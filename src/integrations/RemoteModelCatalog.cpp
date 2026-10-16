#include "RemoteModelCatalog.h"

#include "ApiKeyBinding.h"
#include "ApiKeyRegistry.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <array>
#include <utility>

namespace {
constexpr qsizetype kMaximumResponseBytes = 1024 * 1024;
constexpr qsizetype kMaximumModels = 4096;
constexpr int kTimeoutMs = 15 * 1000;

bool isValidCategory(const QString &category) {
    return category == QStringLiteral("speech-recognition") || category == QStringLiteral("speech-output")
        || category == QStringLiteral("chat") || category == QStringLiteral("embedding");
}

bool isGroq(const QUrl &url) {
    return url.host().compare(QStringLiteral("api.groq.com"), Qt::CaseInsensitive) == 0;
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

QString modelsPathFor(const QUrl &endpoint) {
    QString path = endpoint.path();
    while (path.size() > 1 && path.endsWith(QLatin1Char('/'))) path.chop(1);
    if (path.endsWith(QStringLiteral("/models"))) return path;
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
    return QStringLiteral("Groq's combined list excludes known speech and embedding families for chat. Model identifiers remain enterable manually.");
}

bool includeForCategory(const QString &model, const QString &category, const bool groq) {
    if (!groq) return true;
    if (category == QStringLiteral("speech-recognition")) return isSpeechRecognitionModel(model);
    if (category == QStringLiteral("speech-output")) return isSpeechOutputModel(model);
    if (category == QStringLiteral("embedding")) return isEmbeddingModel(model);
    return !isSpeechRecognitionModel(model) && !isSpeechOutputModel(model) && !isEmbeddingModel(model);
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
    m_error.clear();
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
                m_error.clear();
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
    m_error.clear();
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
    m_error.clear();
    emit configurationChanged();
    emit changed();
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
    if (m_busy) {
        m_busy = false;
        emit changed();
    }
}

void RemoteModelCatalog::cancel() {
    cancelRequest();
    m_status.clear();
    m_error.clear();
    emit changed();
}

void RemoteModelCatalog::fail(const QString &code, const QString &message) {
    m_busy = false;
    m_status.clear();
    m_error = code + QStringLiteral(": ") + message;
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
    const QString path = modelsPathFor(endpoint);
    if (path.isEmpty()) {
        fail(QStringLiteral("MODELS_ENDPOINT"), QStringLiteral("The endpoint path is not a supported OpenAI-compatible model endpoint."));
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
    QUrl modelsUrl;
    QByteArray authorization;
    if (!prepareRequest(&modelsUrl, &authorization)) return;
    m_error.clear();
    m_status = QStringLiteral("Fetching available models...");
    m_busy = true;
    const quint64 generation = m_generation;
    QNetworkRequest request(modelsUrl);
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(kTimeoutMs);
    if (!authorization.isEmpty()) request.setRawHeader("Authorization", authorization);
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
    QTimer::singleShot(kTimeoutMs, this, [this, reply, generation] {
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
    if (!okay) {
        reply->deleteLater();
        if (httpStatus == 401 || httpStatus == 403) {
            fail(QStringLiteral("MODELS_AUTH"), QStringLiteral("The provider rejected the selected API key. Choose or save a key for this provider."));
        } else if (httpStatus <= 0) {
            fail(QStringLiteral("MODELS_NETWORK"), QStringLiteral("The model list could not be reached. Check the endpoint and connection, then retry."));
        } else {
            fail(QStringLiteral("MODELS_HTTP_%1").arg(httpStatus), QStringLiteral("The provider did not return its model list. Check the endpoint and model service."));
        }
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    const QJsonValue dataValue = document.isObject() ? document.object().value(QStringLiteral("data")) : QJsonValue();
    const QJsonArray data = dataValue.toArray();
    if (parseError.error != QJsonParseError::NoError || !document.isObject() || !dataValue.isArray() || data.size() > kMaximumModels) {
        reply->deleteLater();
        fail(QStringLiteral("MODELS_RESPONSE"), QStringLiteral("The provider returned an invalid or empty model list."));
        return;
    }
    if (data.isEmpty()) {
        reply->deleteLater();
        fail(QStringLiteral("MODELS_EMPTY"), QStringLiteral("The provider returned no models. You can still enter a model identifier manually."));
        return;
    }
    const QUrl endpoint(m_endpoint.trimmed());
    QStringList discovered;
    for (const QJsonValue &value : data) {
        if (!value.isObject()) continue;
        const QJsonObject model = value.toObject();
        const QJsonValue id = model.value(QStringLiteral("id"));
        if (!id.isString()) continue;
        const QString name = id.toString();
        if (model.value(QStringLiteral("active")).isBool() && !model.value(QStringLiteral("active")).toBool()) continue;
        const QString credential = m_binding ? m_binding->key() : QString();
        const bool echoesCredential = !credential.isEmpty() && name == credential;
        if (isAllowedModelId(name) && !echoesCredential && includeForCategory(name, m_category, isGroq(endpoint)) && !discovered.contains(name)) discovered.append(name);
    }
    std::sort(discovered.begin(), discovered.end(), [](const QString &a, const QString &b) {
        const int comparison = QString::compare(a, b, Qt::CaseInsensitive);
        return comparison == 0 ? a < b : comparison < 0;
    });
    reply->deleteLater();
    if (discovered.isEmpty()) {
        fail(QStringLiteral("MODELS_CATEGORY"), QStringLiteral("The provider returned no models recognized for this category. You can still enter a model identifier manually."));
        m_categoryHint = modelHint(endpoint, m_category);
        emit changed();
        return;
    }
    m_models = std::move(discovered);
    m_categoryHint = modelHint(endpoint, m_category);
    m_busy = false;
    m_status = QStringLiteral("Available models updated.");
    m_error.clear();
    emit changed();
}
