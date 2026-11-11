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
    : QObject(parent), m_credentials(registry->binding(QStringLiteral("web-search"), this)),
      m_enabled(m_settings.value(QStringLiteral("webSearch/enabled"), false).toBool()) {
    Q_ASSERT(registry && m_credentials);
    m_credentials->setEndpoint(endpoint);
    m_credentialSnapshot = m_credentials->key();
    connect(m_credentials, &ApiKeyBinding::changed, this, [this] {
        const QString key = m_credentials->key();
        if (key != m_credentialSnapshot) {
            m_credentialSnapshot = key;
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
void WebSearch::search(const QString &query, QObject *context, Completion callback) {
    Q_ASSERT(context && callback && !m_reply);
    if (!context || !callback || m_reply) return;
    if (!m_enabled || !hasApiKey()) {
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
        {QStringLiteral("search_depth"), QStringLiteral("basic")}, {QStringLiteral("max_results"), maximumResults},
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
