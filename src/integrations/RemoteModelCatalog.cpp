#include "RemoteModelCatalog.h"

#include "ApiKeyBinding.h"
#include "ApiKeyRegistry.h"
#include "core/DiagnosticLog.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <array>
#include <utility>

namespace {
constexpr qsizetype kMaximumResponseBytes = 1024 * 1024;
constexpr qsizetype kMaximumModels = 4096;
constexpr int kTimeoutMs = 15 * 1000;
constexpr int kMaximumPages = 5;

bool isValidCategory(const QString &category) {
    return category == QStringLiteral("speech-recognition") || category == QStringLiteral("speech-output")
        || category == QStringLiteral("chat") || category == QStringLiteral("embedding");
}

bool isGroq(const QUrl &url) {
    const bool https = url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0;
    const bool defaultPort = url.port() < 0 || url.port() == 443;
    return https && defaultPort && url.host().compare(QStringLiteral("api.groq.com"), Qt::CaseInsensitive) == 0;
}

bool isSpeechRecognitionModel(const QString &model) {
    return model.startsWith(QStringLiteral("whisper-"), Qt::CaseInsensitive)
        || model.startsWith(QStringLiteral("distil-whisper-"), Qt::CaseInsensitive);
}

bool isSpeechOutputModel(const QString &model) {
    return model.startsWith(QStringLiteral("canopylabs/orpheus-"), Qt::CaseInsensitive)
        || model.startsWith(QStringLiteral("playai-tts"), Qt::CaseInsensitive);
}

bool isEmbeddingModel(const QString &model) {
    return model.startsWith(QStringLiteral("text-embedding"), Qt::CaseInsensitive)
        || model.startsWith(QStringLiteral("bge-"), Qt::CaseInsensitive)
        || model.startsWith(QStringLiteral("nomic-embed"), Qt::CaseInsensitive)
        || model.contains(QStringLiteral("embedding"), Qt::CaseInsensitive);
}

bool isSafetyClassifierModel(const QString &model) {
    return model.startsWith(QStringLiteral("meta-llama/llama-prompt-guard"), Qt::CaseInsensitive)
        || model.startsWith(QStringLiteral("meta-llama/llama-guard"), Qt::CaseInsensitive)
        || model.startsWith(QStringLiteral("llama-prompt-guard"), Qt::CaseInsensitive)
        || model.startsWith(QStringLiteral("llama-guard"), Qt::CaseInsensitive)
        || model.startsWith(QStringLiteral("openai/gpt-oss-safeguard"), Qt::CaseInsensitive)
        || model.startsWith(QStringLiteral("gpt-oss-safeguard"), Qt::CaseInsensitive);
}

bool isAllowedModelId(const QString &model) {
    if (model.isEmpty() || model.size() > 256) return false;
    for (const QChar character : model) {
        const ushort code = character.unicode();
        const bool asciiLetter = (code >= 'a' && code <= 'z') || (code >= 'A' && code <= 'Z');
        const bool asciiDigit = code >= '0' && code <= '9';
        const bool punctuation = code == '.' || code == '_' || code == ':' || code == '/' || code == '-';
        if (!asciiLetter && !asciiDigit && !punctuation)
            return false;
    }
    return true;
}

QString modelsPathFor(const QUrl &endpoint, const bool anthropic) {
    QString path = endpoint.path();
    while (path.size() > 1 && path.endsWith(QLatin1Char('/'))) path.chop(1);
    if (path.endsWith(QStringLiteral("/models"))) return path;
    if (anthropic) {
        const QString suffix = QStringLiteral("/messages");
        return path.endsWith(suffix) ? path.left(path.size() - suffix.size()) + QStringLiteral("/models") : QString();
    }
    const std::array suffixes{
        QStringLiteral("/audio/transcriptions"), QStringLiteral("/audio/translations"),
        QStringLiteral("/audio/speech"), QStringLiteral("/chat/completions"),
        QStringLiteral("/embeddings")};
    for (const QString &suffix : suffixes) {
        if (path.endsWith(suffix)) return path.left(path.size() - suffix.size()) + QStringLiteral("/models");
    }
    return {};
}

QString modelHint(const QUrl &endpoint, const QString &category) {
    if (!isGroq(endpoint))
        return QStringLiteral("The provider returned a combined model list. Choose a compatible model or enter its identifier manually.");
    if (category == QStringLiteral("speech-recognition"))
        return QStringLiteral("Groq's combined list is filtered to known Whisper speech-recognition families. Model identifiers remain enterable manually.");
    if (category == QStringLiteral("speech-output"))
        return QStringLiteral("Groq's combined list is filtered to known speech-output families. Model identifiers remain enterable manually.");
    if (category == QStringLiteral("embedding"))
        return QStringLiteral("Groq's combined list is filtered to known embedding-style identifiers. Model identifiers remain enterable manually.");
    return QStringLiteral("Groq's combined list excludes known speech, embedding, and safety-classifier families for chat. Model identifiers remain enterable manually.");
}

bool includeForCategory(const QString &model, const QString &category, const bool groq) {
    if (!groq) return true;
    if (category == QStringLiteral("speech-recognition")) return isSpeechRecognitionModel(model);
    if (category == QStringLiteral("speech-output")) return isSpeechOutputModel(model);
    if (category == QStringLiteral("embedding")) return isEmbeddingModel(model);
    return !isSpeechRecognitionModel(model) && !isSpeechOutputModel(model) && !isEmbeddingModel(model)
        && !isSafetyClassifierModel(model);
}
}

RemoteModelCatalog::RemoteModelCatalog(QObject *parent)
    : QObject(parent) {}

RemoteModelCatalog::~RemoteModelCatalog() {
    cancel();
    QObject::disconnect(m_bindingChangedConnection);
    QObject::disconnect(m_bindingDestroyedConnection);
}

QObject *RemoteModelCatalog::apiKey() const { return m_binding; }
QString RemoteModelCatalog::endpoint() const { return m_endpoint; }
QString RemoteModelCatalog::category() const { return m_category; }
QString RemoteModelCatalog::remoteApi() const { return m_remoteApi; }
QString RemoteModelCatalog::workspaceId() const { return m_workspaceId; }
bool RemoteModelCatalog::active() const { return m_active; }
bool RemoteModelCatalog::busy() const { return m_busy; }
QStringList RemoteModelCatalog::models() const { return m_models; }
QString RemoteModelCatalog::status() const { return m_status; }
QString RemoteModelCatalog::error() const { return m_error; }
QString RemoteModelCatalog::categoryHint() const { return m_categoryHint; }

void RemoteModelCatalog::setApiKey(QObject *const bindingObject) {
    ApiKeyBinding *const binding = qobject_cast<ApiKeyBinding *>(bindingObject);
    if (binding == m_binding) return;
    QObject::disconnect(m_bindingChangedConnection);
    QObject::disconnect(m_bindingDestroyedConnection);
    m_binding = binding;
    m_credentialId.clear();
    m_credentialDigest.clear();
    m_models.clear();
    m_categoryHint.clear();
    cancelRequest();
    m_status.clear();
    m_error.clear(); m_errorDetail.clear();
    if (m_binding) {
        m_bindingChangedConnection = connect(m_binding, &ApiKeyBinding::changed, this, [this] {
            const QString id = m_binding ? m_binding->selectedKeyId() : QString();
            const QByteArray digest = m_binding ? QCryptographicHash::hash(m_binding->key().toUtf8(), QCryptographicHash::Sha256) : QByteArray();
            if (id != m_credentialId || digest != m_credentialDigest) {
                m_credentialId = id;
                m_credentialDigest = digest;
                m_models.clear();
                m_categoryHint.clear();
                cancelRequest();
                m_status.clear();
                m_error.clear(); m_errorDetail.clear();
                emit changed();
            }
        });
        m_bindingDestroyedConnection = connect(m_binding, &QObject::destroyed, this, [this] {
            m_binding = nullptr;
            m_credentialId.clear();
            m_credentialDigest.clear();
            m_models.clear();
            m_categoryHint.clear();
            cancelRequest();
            fail(QStringLiteral("MODELS_KEY"), QStringLiteral("The API-key binding is unavailable. Select a remote API key before fetching models."));
        });
        refreshCredentialSnapshot();
    }
    emit configurationChanged();
    emit changed();
}

void RemoteModelCatalog::setEndpoint(const QString &endpoint) {
    if (m_endpoint == endpoint) return;
    m_endpoint = endpoint;
    m_models.clear();
    m_categoryHint.clear();
    cancelRequest();
    m_status.clear();
    m_error.clear(); m_errorDetail.clear();
    emit configurationChanged();
    emit changed();
}

void RemoteModelCatalog::setCategory(const QString &category) {
    if (m_category == category) return;
    m_category = category;
    m_models.clear();
    m_categoryHint.clear();
    cancelRequest();
    m_status.clear();
    m_error.clear(); m_errorDetail.clear();
    emit configurationChanged();
    emit changed();
}

void RemoteModelCatalog::setRemoteApi(const QString &remoteApi) {
    if (remoteApi != QStringLiteral("openai") && remoteApi != QStringLiteral("anthropic")) return;
    if (m_remoteApi == remoteApi) return;
    m_remoteApi = remoteApi;
    m_models.clear(); m_categoryHint.clear(); cancelRequest(); m_status.clear(); m_error.clear(); m_errorDetail.clear();
    emit configurationChanged(); emit changed();
}

void RemoteModelCatalog::setWorkspaceId(const QString &workspaceId) {
    if (m_workspaceId == workspaceId) return;
    m_workspaceId = workspaceId;
    m_models.clear(); m_categoryHint.clear(); cancelRequest(); m_status.clear(); m_error.clear(); m_errorDetail.clear();
    emit configurationChanged(); emit changed();
}

void RemoteModelCatalog::setActive(const bool active) {
    if (m_active == active) return;
    m_active = active;
    if (!m_active) {
        cancelRequest();
        m_status.clear();
    }
    emit configurationChanged();
    emit changed();
}

void RemoteModelCatalog::refreshCredentialSnapshot() {
    m_credentialId = m_binding ? m_binding->selectedKeyId() : QString();
    m_credentialDigest = m_binding ? QCryptographicHash::hash(m_binding->key().toUtf8(), QCryptographicHash::Sha256) : QByteArray();
}

void RemoteModelCatalog::cancelRequest() {
    ++m_generation;
    if (m_reply) m_reply->abort();
    m_reply = nullptr;
    m_body.clear();
    m_pendingModels.clear();
    m_seenCursors.clear();
    m_pageCount = 0;
    if (m_busy) {
        m_busy = false;
        emit changed();
    }
}

void RemoteModelCatalog::cancel() {
    cancelRequest();
    m_status.clear();
    m_error.clear(); m_errorDetail.clear();
    emit changed();
}

void RemoteModelCatalog::fail(const QString &code, const QString &message, const QString &detail) {
    m_busy = false;
    m_status.clear();
    m_error = code + QStringLiteral(": ") + message;
    m_errorDetail = detail;
    emit changed();
}

bool RemoteModelCatalog::prepareRequest(QUrl *const modelsUrl, QByteArray *const authorization) {
    Q_ASSERT(modelsUrl && authorization);
    const QUrl endpoint(m_endpoint.trimmed());
    const QString origin = ApiKeyRegistry::canonicalOrigin(m_endpoint);
    if (!endpoint.isValid() || origin.isEmpty() || !isValidCategory(m_category)) {
        fail(QStringLiteral("MODELS_ENDPOINT"), QStringLiteral("Configure a supported remote model endpoint before fetching models."));
        return false;
    }
    const bool anthropic = m_remoteApi == QStringLiteral("anthropic");
    if (anthropic && m_category != QStringLiteral("chat")) {
        fail(QStringLiteral("MODELS_CATEGORY"), QStringLiteral("Anthropic model discovery is available for language models only."));
        return false;
    }
    static const QRegularExpression workspacePattern(QStringLiteral("^[A-Za-z0-9_-]{1,256}$"));
    if (anthropic && !m_workspaceId.isEmpty() && !workspacePattern.match(m_workspaceId).hasMatch()) {
        fail(QStringLiteral("MODELS_WORKSPACE"), QStringLiteral("The Anthropic workspace ID is invalid. Use only letters, digits, underscores or hyphens, up to 256 characters."));
        return false;
    }
    const QString path = modelsPathFor(endpoint, anthropic);
    if (path.isEmpty()) {
        fail(QStringLiteral("MODELS_ENDPOINT"), anthropic ? QStringLiteral("Use an Anthropic Messages endpoint ending in /messages to fetch its model list.")
                              : QStringLiteral("The endpoint path is not a supported OpenAI-compatible model endpoint."));
        return false;
    }
    QUrl derived = endpoint;
    derived.setPath(path);
    derived.setQuery(QString());
    derived.setFragment(QString());
    if (ApiKeyRegistry::canonicalOrigin(derived.toString()) != origin) {
        fail(QStringLiteral("MODELS_ENDPOINT"), QStringLiteral("The model catalog endpoint must use the same provider origin."));
        return false;
    }
    const bool localhost = endpoint.host().compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0
        || endpoint.host() == QStringLiteral("127.0.0.1") || endpoint.host() == QStringLiteral("::1");
    const QString selectedId = m_binding ? m_binding->selectedKeyId() : QString();
    const bool validSelection = m_binding && m_binding->registry()->canSelectKey(selectedId, m_endpoint);
    if (!localhost && (!validSelection || !m_binding->hasKey())) {
        const QString state = m_binding ? m_binding->storageState() : QString();
        if (state == QStringLiteral("loading")) {
            fail(QStringLiteral("MODELS_KEY"), QStringLiteral("The API-key storage is still loading. Try fetching again shortly."));
        } else {
            fail(QStringLiteral("MODELS_KEY"), QStringLiteral("Select or save an API key for this provider before fetching models."));
        }
        return false;
    }
    if (validSelection && m_binding->hasKey()) *authorization = "Bearer " + m_binding->key().toUtf8();
    *modelsUrl = derived;
    return true;
}

void RemoteModelCatalog::fetch() {
    if (!m_active) { fail(QStringLiteral("MODELS_INACTIVE"), QStringLiteral("Model discovery is available only for a remote model configuration.")); return; }
    cancelRequest();
    m_error.clear(); m_errorDetail.clear();
    m_status = QStringLiteral("Fetching available models...");
    m_busy = true;
    fetchPage({});
}

void RemoteModelCatalog::fetchPage(const QString &afterId) {
    Q_ASSERT(m_busy);
    QUrl modelsUrl;
    QByteArray authorization;
    if (!prepareRequest(&modelsUrl, &authorization)) return;
    if (m_remoteApi == QStringLiteral("anthropic")) {
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("limit"), QStringLiteral("100"));
        if (!afterId.isEmpty()) query.addQueryItem(QStringLiteral("after_id"), afterId);
        modelsUrl.setQuery(query);
    }
    ++m_pageCount;
    const quint64 generation = m_generation;
    QNetworkRequest request(modelsUrl);
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(kTimeoutMs);
    if (!authorization.isEmpty()) request.setRawHeader("Authorization", authorization);
    if (m_remoteApi == QStringLiteral("anthropic")) {
        request.setRawHeader("anthropic-version", "2026-10-21");
        if (!m_workspaceId.isEmpty()) request.setRawHeader("anthropic-workspace-id", m_workspaceId.toUtf8());
    }
    m_body.clear();
    m_responseTooLarge = false;
    m_reply = m_network.get(request);
    QNetworkReply *const reply = m_reply;
    reply->setReadBufferSize(kMaximumResponseBytes + 1);
    connect(reply, &QIODevice::readyRead, this, [this, reply] {
        if (m_reply != reply) return;
        const qint64 remaining = static_cast<qint64>(kMaximumResponseBytes) + 1 - m_body.size();
        if (remaining <= 0) {
            m_responseTooLarge = true;
            reply->abort();
            return;
        }
        m_body.append(reply->read(remaining));
        if (m_body.size() > kMaximumResponseBytes) {
            m_responseTooLarge = true;
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] { finishReply(reply, generation); });
    QTimer::singleShot(kTimeoutMs, reply, [this, reply, generation] {
        if (m_reply != reply || generation != m_generation || reply->isFinished()) return;
        ++m_generation;
        m_reply = nullptr;
        m_body.clear();
        reply->abort();
        reply->deleteLater();
        m_busy = false;
        m_status.clear();
        m_error = QStringLiteral("MODELS_TIMEOUT: The provider did not return its model list within 15 seconds. Check the endpoint and connection, then retry.");
        emit changed();
    });
    emit changed();
}

void RemoteModelCatalog::finishReply(QNetworkReply *const reply, const quint64 generation) {
    if (m_reply != reply || generation != m_generation) { reply->deleteLater(); return; }
    m_reply = nullptr;
    const qint64 remaining = static_cast<qint64>(kMaximumResponseBytes) + 1 - m_body.size();
    if (remaining <= 0 || reply->bytesAvailable() > remaining) {
        m_responseTooLarge = true;
    } else {
        m_body.append(reply->read(remaining));
    }
    const QByteArray body = std::exchange(m_body, {});
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool redirected = httpStatus >= 300 && httpStatus < 400;
    const bool timedOut = reply->error() == QNetworkReply::TimeoutError;
    const bool okay = reply->error() == QNetworkReply::NoError && httpStatus >= 200 && httpStatus < 300;
    const bool tooLarge = m_responseTooLarge || body.size() > kMaximumResponseBytes;
    m_responseTooLarge = false;
    if (tooLarge) {
        reply->deleteLater();
        fail(QStringLiteral("MODELS_RESPONSE_TOO_LARGE"), QStringLiteral("The provider returned more than 1 MiB of model data."));
        return;
    }
    if (redirected) {
        reply->deleteLater();
        fail(QStringLiteral("MODELS_REDIRECT"), QStringLiteral("The model endpoint redirected the request. Use the provider's final HTTPS endpoint."));
        return;
    }
    if (timedOut) {
        reply->deleteLater();
        fail(QStringLiteral("MODELS_TIMEOUT"), QStringLiteral("The provider did not return its model list within 15 seconds. Check the endpoint and connection, then retry."));
        return;
    }
    const auto responseDetail = [&]() {
        QString networkDetail = reply->errorString();
        const QUrl requestUrl = reply->url();
        const QString credential = m_binding ? m_binding->key() : QString();
        QString response = QString::fromUtf8(body);
        if (!credential.isEmpty()) {
            response.replace(credential, QStringLiteral("[redacted]"));
            networkDetail.replace(credential, QStringLiteral("[redacted]"));
        }
        if (auto *const log = DiagnosticLog::instance()) {
            response = log->redact(response);
            networkDetail = log->redact(networkDetail);
        }
        const bool truncated = response.size() > 16000;
        response = response.left(16000);
        return QStringLiteral("Endpoint: %1\nHTTP status: %2\nNetwork: %3\nResponse body%4:\n%5")
            .arg(requestUrl.toString(QUrl::RemoveUserInfo | QUrl::RemoveQuery | QUrl::RemoveFragment))
            .arg(httpStatus).arg(networkDetail.left(1000), truncated ? QStringLiteral(" (truncated)") : QString(),
                                  response.isEmpty() ? QStringLiteral("(empty)") : response);
    };
    if (!okay) {
        const QString detail = responseDetail();
        reply->deleteLater();
        if (httpStatus == 401) {
            fail(QStringLiteral("MODELS_HTTP_401"), QStringLiteral("The provider rejected the API key (HTTP 401). Select or save a valid key for this provider."), detail);
        } else if (httpStatus == 403) {
            fail(QStringLiteral("MODELS_HTTP_403"), QStringLiteral("The provider denied model-list access (HTTP 403). Check the key's permissions and workspace."), detail);
        } else if (httpStatus == 429) {
            fail(QStringLiteral("MODELS_HTTP_429"), QStringLiteral("The provider declined the model-list request (HTTP 429). Check its rate or credit limits, then retry."), detail);
        } else if (httpStatus <= 0) {
            fail(QStringLiteral("MODELS_NETWORK"), QStringLiteral("The model list could not be reached. Check the endpoint and connection, then retry."), detail);
        } else {
            fail(QStringLiteral("MODELS_HTTP_%1").arg(httpStatus), QStringLiteral("The provider did not return its model list (HTTP %1). Check the endpoint and provider response, then retry.").arg(httpStatus), detail);
        }
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    const QJsonValue dataValue = document.isObject() ? document.object().value(QStringLiteral("data")) : QJsonValue();
    const QJsonArray data = dataValue.toArray();
    if (parseError.error != QJsonParseError::NoError || !document.isObject() || !dataValue.isArray() || data.size() > kMaximumModels) {
        reply->deleteLater();
        fail(QStringLiteral("MODELS_RESPONSE"), QStringLiteral("The provider returned an invalid model list. Check the response or enter a model identifier manually."),
             QStringLiteral("JSON parse status: %1\n%2").arg(parseError.errorString(), responseDetail()));
        return;
    }
    if (data.isEmpty() && m_pendingModels.isEmpty()) {
        reply->deleteLater();
        fail(QStringLiteral("MODELS_EMPTY"), QStringLiteral("The provider returned no models. You can still enter a model identifier manually."), responseDetail());
        return;
    }
    const QUrl endpoint(m_endpoint.trimmed());
    QStringList discovered = m_pendingModels;
    bool modelLimitReached = false;
    for (const QJsonValue &value : data) {
        if (!value.isObject()) continue;
        const QJsonObject model = value.toObject();
        const QJsonValue id = model.value(QStringLiteral("id"));
        if (!id.isString()) continue;
        const QString name = id.toString();
        if (model.value(QStringLiteral("active")).isBool() && !model.value(QStringLiteral("active")).toBool()) continue;
        const QString credential = m_binding ? m_binding->key() : QString();
        const bool echoesCredential = !credential.isEmpty() && name.contains(credential);
        if (isAllowedModelId(name) && !echoesCredential && includeForCategory(name, m_category, isGroq(endpoint)) && !discovered.contains(name)) {
            if (discovered.size() >= kMaximumModels) { modelLimitReached = true; break; }
            discovered.append(name);
        }
    }
    const QJsonObject root = document.object();
    const QJsonValue more = root.value(QStringLiteral("has_more"));
    if (m_remoteApi == QStringLiteral("anthropic") && !more.isBool()) {
        reply->deleteLater();
        fail(QStringLiteral("MODELS_RESPONSE"), QStringLiteral("The Anthropic model response did not contain a valid pagination state. Try again or enter a model identifier manually."), responseDetail());
        return;
    }
    const bool hasMore = m_remoteApi == QStringLiteral("anthropic") && more.toBool();
    const QString cursor = root.value(QStringLiteral("last_id")).toString();
    if (hasMore && (data.isEmpty() || !isAllowedModelId(cursor) || m_seenCursors.contains(cursor))) {
        reply->deleteLater();
        fail(QStringLiteral("MODELS_PAGINATION"), QStringLiteral("The Anthropic model list returned an invalid or repeated page cursor. Try again or enter a model identifier manually."), responseDetail());
        return;
    }
    if (hasMore && m_pageCount < kMaximumPages && !modelLimitReached && discovered.size() < kMaximumModels) {
        m_seenCursors.insert(cursor);
        m_pendingModels = std::move(discovered);
        reply->deleteLater();
        fetchPage(cursor);
        return;
    }
    std::sort(discovered.begin(), discovered.end(), [](const QString &a, const QString &b) {
        const int comparison = QString::compare(a, b, Qt::CaseInsensitive);
        return comparison == 0 ? a < b : comparison < 0;
    });
    reply->deleteLater();
    if (discovered.isEmpty()) {
        fail(QStringLiteral("MODELS_CATEGORY"), QStringLiteral("The provider returned no models recognized for this category. You can still enter a model identifier manually."), responseDetail());
        m_categoryHint = modelHint(endpoint, m_category);
        emit changed();
        return;
    }
    const qsizetype discoveredCount = discovered.size();
    m_models = std::move(discovered);
    m_categoryHint = m_remoteApi == QStringLiteral("anthropic")
        ? QStringLiteral("Choose an available Claude model or enter its identifier manually.") : modelHint(endpoint, m_category);
    m_pendingModels.clear(); m_seenCursors.clear(); m_pageCount = 0;
    m_busy = false;
    m_status = modelLimitReached || (hasMore && discoveredCount >= kMaximumModels)
        ? QStringLiteral("The model list reached its %1-model limit. Enter a model identifier manually if it is missing.").arg(kMaximumModels)
        : hasMore ? QStringLiteral("Only the first %1 pages of models are shown. Enter a model identifier manually if it is missing.").arg(kMaximumPages)
                  : QStringLiteral("Available models updated.");
    m_error.clear(); m_errorDetail.clear();
    emit changed();
}
