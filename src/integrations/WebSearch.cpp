#include "WebSearch.h"
#include "ApiKeyBinding.h"
#include "ApiKeyRegistry.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace {
constexpr qsizetype responseLimit = 256 * 1024;
constexpr int maximumResults = 5;
static_assert(responseLimit > 0 && maximumResults > 0);
const QString endpoint = QStringLiteral("https://api.tavily.com/search");
}

WebSearch::WebSearch(ApiKeyRegistry *registry, QObject *parent)
    : QObject(parent), m_registry(registry), m_credentials(registry->binding(QStringLiteral("web-search"), this)),
      m_enabled(m_settings.value(QStringLiteral("webSearch/enabled"), false).toBool()) {
    Q_ASSERT(registry && m_credentials);
    m_credentials->setEndpoint(endpoint);
    m_credentialSnapshot = m_credentials->key();
    connect(m_credentials, &ApiKeyBinding::changed, this, [this] {
        const QString key = m_credentials->key();
        if (key != m_credentialSnapshot) {
            m_credentialSnapshot = key;
            cancelTest();
            clearTestResult();
            cancel();
            emit configurationChanged();
        }
        emit changed();
    });
}
WebSearch::~WebSearch() { cancel(); }
bool WebSearch::hasApiKey() const { return m_credentials->hasKey(); }
QObject *WebSearch::apiKey() const { return m_credentials; }
void WebSearch::setEnabled(const bool enabled) {
    if (enabled == m_enabled) return;
    m_enabled = enabled;
    m_settings.setValue(QStringLiteral("webSearch/enabled"), enabled);
    cancel();
    emit configurationChanged();
    emit changed();
}
void WebSearch::cancel() {
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    m_context.clear(); m_callback = {}; m_body.clear();
}
void WebSearch::clearTestResult() {
    m_testStatus.clear();
    m_testInput.clear();
    m_testResult.clear();
    m_testErrorCode.clear();
    m_testErrorDetail.clear();
}

void WebSearch::cancelTest() {
    if (!m_testBusy) return;
    Q_ASSERT(m_testSearch);
    m_testSearch->cancel();
    m_testBusy = false;
    m_testResult = QStringLiteral("Request canceled; no result was received.");
    m_testStatus = QStringLiteral("Connection test canceled.");
    emit changed();
}

void WebSearch::testConnection() {
    if (m_testBusy) return;
    clearTestResult();
    if (!hasApiKey()) {
        m_testErrorCode = QStringLiteral("WEB_SEARCH_CONFIG");
        m_testStatus = QStringLiteral("Tavily connection test could not start. Save a Tavily API key and try again.");
        emit changed();
        return;
    }
    if (!m_testSearch) m_testSearch = new WebSearch(m_registry, this);
    m_testInput = QStringLiteral("What is the capital of France?");
    m_testBusy = true;
    m_testStatus = QStringLiteral("Testing Tavily connection...");
    emit changed();
    m_testSearch->startSearch(m_testInput, this,
        [this](std::expected<QVariantList, struct ProviderError> result) {
            m_testBusy = false;
            if (result) {
                m_testStatus = QStringLiteral("Tavily connection succeeded. The API key can perform searches.");
                QStringList excerpts;
                for (const QVariant &item : *result) {
                    const QVariantMap entry = item.toMap();
                    excerpts.append(entry.value(QStringLiteral("title")).toString()
                        + QLatin1Char('\n') + entry.value(QStringLiteral("url")).toString()
                        + QLatin1Char('\n') + entry.value(QStringLiteral("content")).toString());
                }
                m_testResult = excerpts.isEmpty() ? QStringLiteral("Tavily returned no search results.")
                                                : excerpts.join(QStringLiteral("\n\n"));
                const QString credential = m_credentials->key();
                if (!credential.isEmpty()) m_testResult.replace(credential, QStringLiteral("[redacted]"));
            } else {
                m_testResult = QStringLiteral("No search results were received. See the connection error above.");
                const struct ProviderError &error = result.error();
                m_testErrorCode = error.code;
                m_testErrorDetail = error.message;
                if (!error.detail.isEmpty()) m_testErrorDetail += QStringLiteral("\n") + error.detail;
                if (error.code == QStringLiteral("WEB_SEARCH_HTTP_401") || error.code == QStringLiteral("WEB_SEARCH_HTTP_403"))
                    m_testStatus = QStringLiteral("Tavily rejected the connection. Check the API key and account access, then try again.");
                else if (error.code == QStringLiteral("WEB_SEARCH_HTTP_429"))
                    m_testStatus = QStringLiteral("Tavily's rate limit was reached. Wait and try again.");
                else if (error.code == QStringLiteral("WEB_SEARCH_HTTP_432"))
                    m_testStatus = QStringLiteral("Tavily's plan credit limit was reached. Check the reset date or upgrade your plan.");
                else if (error.code == QStringLiteral("WEB_SEARCH_HTTP_433"))
                    m_testStatus = QStringLiteral("Tavily's pay-as-you-go billing limit was reached. Check your billing limit before retrying.");
                else if (error.code == QStringLiteral("WEB_SEARCH_HTTP_434"))
                    m_testStatus = QStringLiteral("Tavily's search capacity is temporarily unavailable. Wait and try again.");
                else if (error.code == QStringLiteral("WEB_SEARCH_TIMEOUT"))
                    m_testStatus = QStringLiteral("Tavily did not respond in time. Check your connection and try again.");
                else
                    m_testStatus = QStringLiteral("Tavily connection test failed. Check the details and try again.");
            }
            emit changed();
        }, false);
}

void WebSearch::search(const QString &query, QObject *context, Completion callback) {
    startSearch(query, context, std::move(callback), true);
}

void WebSearch::startSearch(const QString &query, QObject *context, Completion callback, const bool requireEnabled) {
    Q_ASSERT(context && callback && !m_reply);
    if (!context || !callback || m_reply) return;
    if ((requireEnabled && !m_enabled) || !hasApiKey()) {
        callback(std::unexpected(ProviderError{QStringLiteral("WEB_SEARCH_CONFIG"),
            QStringLiteral("Web search is disabled or its Tavily API key is unavailable. Enable web search and add a key in Settings, or run the check without web search.")}));
        return;
    }
    if (query.trimmed().isEmpty() || query.size() > 400) {
        callback(std::unexpected(ProviderError{QStringLiteral("WEB_SEARCH_QUERY"),
            QStringLiteral("The model requested an empty search or a query longer than 400 characters. Try again or choose another model.")}));
        return;
    }
    QNetworkRequest request{QUrl(endpoint)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_credentials->key().toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    const QJsonObject data{{QStringLiteral("query"), query.trimmed()},
        {QStringLiteral("search_depth"), QStringLiteral("basic")}, {QStringLiteral("max_results"), requireEnabled ? maximumResults : 1},
        {QStringLiteral("auto_parameters"), false}, {QStringLiteral("include_answer"), false},
        {QStringLiteral("include_raw_content"), false}, {QStringLiteral("include_images"), false}};
    m_context = context; m_callback = std::move(callback); m_body.clear(); m_tooLarge = false; m_timedOut = false;
    QNetworkReply *const reply = m_network.post(request, QJsonDocument(data).toJson(QJsonDocument::Compact));
    m_reply = reply;
    reply->setReadBufferSize(128 * 1024);
    QTimer::singleShot(30000, reply, [this, reply] {
        if (m_reply == reply && !reply->isFinished()) { m_timedOut = true; reply->abort(); }
    });
    connect(reply, &QIODevice::readyRead, this, [this, reply] {
        if (m_reply != reply) return;
        m_body.append(reply->readAll());
        if (m_body.size() > responseLimit) { m_tooLarge = true; reply->abort(); }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (m_reply != reply) { reply->deleteLater(); return; }
        m_body.append(reply->readAll());
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto networkError = reply->error();
        QString networkMessage = reply->errorString();
        const QString credential = m_credentials->key();
        if (!credential.isEmpty()) networkMessage.replace(credential, QStringLiteral("[redacted]"));
        networkMessage = networkMessage.left(256);
        const QByteArray body = m_body;
        const bool timedOut = m_timedOut, tooLarge = m_tooLarge || body.size() > responseLimit;
        const QPointer<QObject> context = m_context;
        Completion callback = std::move(m_callback);
        m_reply = nullptr; m_context.clear(); m_body.clear(); reply->deleteLater();
        if (!context || !callback) return;
        if (tooLarge) { callback(std::unexpected(ProviderError{QStringLiteral("WEB_SEARCH_RESPONSE_LIMIT"), QStringLiteral("Tavily returned more than 256 KiB. Try again.")})); return; }
        if (timedOut) { callback(std::unexpected(ProviderError{QStringLiteral("WEB_SEARCH_TIMEOUT"), QStringLiteral("Tavily did not respond within 30 seconds. Check the connection and try again.")})); return; }
        if (networkError != QNetworkReply::NoError || status < 200 || status >= 300) {
            const QString code = status ? QStringLiteral("WEB_SEARCH_HTTP_%1").arg(status) : QStringLiteral("WEB_SEARCH_NETWORK");
            QString advice;
            if (status == 401 || status == 403) advice = QStringLiteral("Check the Tavily API key and account access in Settings.");
            else if (status == 429) advice = QStringLiteral("Tavily's rate limit was reached. Wait and try again.");
            else if (status == 432) advice = QStringLiteral("Tavily's plan credit limit was reached. Check the credit reset date or upgrade the plan.");
            else if (status == 433) advice = QStringLiteral("Tavily's pay-as-you-go billing limit was reached. Check your billing limit before retrying.");
            else if (status == 434) advice = QStringLiteral("Tavily's real-time search capacity is temporarily unavailable. Wait and try again.");
            else if (status >= 300 && status < 400) advice = QStringLiteral("Tavily returned a redirect, which was not followed. Check the service status.");
            else advice = QStringLiteral("Check the Tavily service and network connection, then try again.");
            const QJsonObject failure = QJsonDocument::fromJson(body).object();
            const QJsonValue detail = failure.value(QStringLiteral("detail"));
            QString providerMessage = detail.isString() ? detail.toString()
                : detail.toObject().value(QStringLiteral("error")).toString();
            if (providerMessage.isEmpty()) providerMessage = failure.value(QStringLiteral("message")).toString();
            if (!credential.isEmpty()) providerMessage.replace(credential, QStringLiteral("[redacted]"));
            providerMessage = providerMessage.simplified().left(512);
            const QString observed = QStringLiteral("Tavily search failed (HTTP %1, network error %2: %3). %4")
                .arg(status).arg(static_cast<int>(networkError)).arg(networkMessage, advice);
            callback(std::unexpected(ProviderError{code, observed
                + (providerMessage.isEmpty() ? QString() : QStringLiteral(" Provider detail: ") + providerMessage)}));
            return;
        }
        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject() || !document.object().value(QStringLiteral("results")).isArray()) {
            callback(std::unexpected(ProviderError{QStringLiteral("WEB_SEARCH_RESPONSE"), QStringLiteral("Tavily returned an unreadable search response. Try again or check its service status.")})); return;
        }
        QVariantList results;
        const QJsonArray items = document.object().value(QStringLiteral("results")).toArray();
        for (const QJsonValue &item : items) {
            if (results.size() >= maximumResults) break;
            const QJsonObject result = item.toObject();
            const QString title = result.value(QStringLiteral("title")).toString().trimmed().left(300);
            const QString rawContent = result.value(QStringLiteral("content")).toString().trimmed();
            const QString content = rawContent.left(2500);
            const QString rawUrl = result.value(QStringLiteral("url")).toString();
            const QUrl url(rawUrl, QUrl::StrictMode);
            if (rawUrl.size() > 2048 || !url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty()
                || (url.scheme() != QStringLiteral("https") && url.scheme() != QStringLiteral("http")) || content.isEmpty()) continue;
            results.append(QVariantMap{{QStringLiteral("title"), title.isEmpty() ? url.host() : title},
                {QStringLiteral("url"), url.toString(QUrl::FullyEncoded)}, {QStringLiteral("content"), content},
                {QStringLiteral("truncated"), content.size() < rawContent.size()}});
        }
        callback(results);
    });
}
