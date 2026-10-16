#include "RemoteTts.h"
#include "integrations/ApiKeyBinding.h"
#include "integrations/ApiKeyRegistry.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSettings>
#include <QFutureWatcher>
#include <QHash>
#include <QtConcurrent/QtConcurrentRun>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace {
constexpr qsizetype kMaximumResponseBytes = 20 * 1024 * 1024;
constexpr qsizetype kMaximumTextCharacters = 8192;
constexpr int kRequestTimeoutMs = 45 * 1000;
constexpr int kOverallTimeoutMs = 180 * 1000;
constexpr qsizetype kMaximumProviderErrorBytes = 64 * 1024;
constexpr quint32 kUnknownWaveSize = std::numeric_limits<quint32>::max();

quint32 readLe32(const char *const p) {
    return static_cast<quint32>(static_cast<unsigned char>(p[0]))
        | (static_cast<quint32>(static_cast<unsigned char>(p[1])) << 8)
        | (static_cast<quint32>(static_cast<unsigned char>(p[2])) << 16)
        | (static_cast<quint32>(static_cast<unsigned char>(p[3])) << 24);
}

quint16 readLe16(const char *const p) {
    return static_cast<quint16>(static_cast<unsigned char>(p[0]))
        | (static_cast<quint16>(static_cast<unsigned char>(p[1])) << 8);
}

qint16 sampleToInt16(const char *const source, const quint16 bits) {
    switch (bits) {
    case 8: return static_cast<qint16>((static_cast<int>(static_cast<unsigned char>(*source)) - 128) << 8);
    case 16: return static_cast<qint16>(readLe16(source));
    case 24: {
        const qint32 value = static_cast<qint32>(static_cast<unsigned char>(source[0]))
            | (static_cast<qint32>(static_cast<unsigned char>(source[1])) << 8)
            | (static_cast<qint32>(static_cast<signed char>(source[2])) << 16);
        return static_cast<qint16>(value >> 8);
    }
    case 32: return static_cast<qint16>(static_cast<qint32>(readLe32(source)) >> 16);
    default: return 0;
    }
}

QByteArray convertPcm(const QByteArray &source, const QAudioFormat &from, const QAudioFormat &to) {
    if (from.sampleFormat() != QAudioFormat::Int16 || from.channelCount() <= 0 || to.channelCount() <= 0
        || from.sampleRate() <= 0 || to.sampleRate() <= 0 || to.bytesPerFrame() <= 0) return {};
    const qint64 sourceFrames = source.size() / from.bytesPerFrame();
    const qint64 targetFrames = static_cast<qint64>(std::ceil(static_cast<double>(sourceFrames) * to.sampleRate() / from.sampleRate()));
    const qint64 bytes = targetFrames * to.bytesPerFrame();
    if (sourceFrames <= 0 || bytes <= 0 || bytes > 64 * 1024 * 1024) return {};
    QByteArray result(static_cast<qsizetype>(bytes), '\0');
    auto sampleAt = [&source, &from](const qint64 frame, const int channel) {
        qint16 sample = 0;
        const qint64 index = frame * from.channelCount() + std::min(channel, from.channelCount() - 1);
        std::memcpy(&sample, source.constData() + index * sizeof(sample), sizeof(sample));
        return static_cast<float>(sample) / 32767.0F;
    };
    char *destination = result.data();
    for (qint64 frame = 0; frame < targetFrames; ++frame) {
        const double position = std::min(static_cast<double>(sourceFrames - 1), static_cast<double>(frame) * from.sampleRate() / to.sampleRate());
        const qint64 lower = static_cast<qint64>(position);
        const qint64 upper = std::min(lower + 1, sourceFrames - 1);
        const float fraction = static_cast<float>(position - lower);
        for (int channel = 0; channel < to.channelCount(); ++channel) {
            const int sourceChannel = from.channelCount() == 1 ? 0 : channel % from.channelCount();
            const float value = std::clamp(sampleAt(lower, sourceChannel) * (1.0F - fraction) + sampleAt(upper, sourceChannel) * fraction, -1.0F, 1.0F);
            switch (to.sampleFormat()) {
            case QAudioFormat::UInt8: { const quint8 sample = static_cast<quint8>(std::lround((value + 1.0F) * 127.5F)); std::memcpy(destination, &sample, 1); destination += 1; break; }
            case QAudioFormat::Int16: { const qint16 sample = static_cast<qint16>(std::lround(value * 32767.0F)); std::memcpy(destination, &sample, 2); destination += 2; break; }
            case QAudioFormat::Int32: { const qint32 sample = static_cast<qint32>(std::llround(static_cast<double>(value) * std::numeric_limits<qint32>::max())); std::memcpy(destination, &sample, 4); destination += 4; break; }
            case QAudioFormat::Float: std::memcpy(destination, &value, 4); destination += 4; break;
            case QAudioFormat::Unknown:
            case QAudioFormat::NSampleFormats: return {};
            }
        }
    }
    return result;
}

QString defaultVoiceFor(const QUrl &endpoint, const QString &model) {
    if (endpoint.scheme() == QStringLiteral("https")
        && endpoint.host().compare(QStringLiteral("api.groq.com"), Qt::CaseInsensitive) == 0
        && (endpoint.port(-1) == -1 || endpoint.port(-1) == 443)) {
        if (model == QStringLiteral("canopylabs/orpheus-arabic-saudi")) return QStringLiteral("fahad");
        if (model == QStringLiteral("canopylabs/orpheus-v1-english")) return QStringLiteral("troy");
    }
    return {};
}
}

RemoteTts::RemoteTts(QObject *parent, ApiKeyRegistry *registry)
    : QObject(parent), m_secret((registry ? registry : new ApiKeyRegistry(this))->binding(QStringLiteral("speech-output"), this)),
      m_network(new QNetworkAccessManager(this)), m_timeout(new QTimer(this)),
      m_overallTimeout(new QTimer(this)) {
    QSettings settings;
    settings.beginGroup(QStringLiteral("voice"));
    settings.beginGroup(QStringLiteral("remoteTts"));
    m_endpoint = settings.value(QStringLiteral("endpoint"), m_endpoint).toString().trimmed();
    m_model = settings.value(QStringLiteral("model"), m_model).toString().trimmed();
    m_voice = settings.value(QStringLiteral("voice"), m_voice).toString().trimmed();
    settings.endGroup();
    settings.endGroup();
    const QStringList configuredVoices = availableVoices();
    if (!configuredVoices.isEmpty() && !configuredVoices.contains(m_voice))
        m_voice = defaultVoiceFor(QUrl(m_endpoint), m_model);
    m_secret->setEndpoint(m_endpoint);
    m_lastKey = m_secret->key();
    m_timeout->setSingleShot(true);
    m_overallTimeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, [this] {
        if (!m_reply) return;
        cancelReply();
        emit failed(QStringLiteral("Remote speech output timed out. Check the endpoint and network connection."), m_request);
        ++m_generation;
    });
    connect(m_overallTimeout, &QTimer::timeout, this, [this] {
        if (!m_chunks.isEmpty()) {
            const quint64 request = m_request;
            cancel();
            emit failed(QStringLiteral("Remote speech output exceeded the overall network deadline."), request);
        }
    });
    connect(m_secret, &ApiKeyBinding::changed, this, [this] {
        emit keyChanged();
        const QString key = m_secret->key();
        if (key != m_lastKey) {
            m_lastKey = key;
            cancel();
            emit credentialChanged();
        }
        emit readinessChanged();
    });
}

RemoteTts::~RemoteTts() { cancel(); }

bool RemoteTts::validEndpoint(const QUrl &url) {
    if (!url.isValid() || !url.userInfo().isEmpty() || !url.query().isEmpty() || !url.fragment().isEmpty()) return false;
    if (url.scheme() == QStringLiteral("https")) return !url.host().isEmpty();
    if (url.scheme() == QStringLiteral("http")) return url.host() == QStringLiteral("localhost") || url.host() == QStringLiteral("127.0.0.1");
    return false;
}

QStringList RemoteTts::voicesFor(const QUrl &endpoint, const QString &model) {
    if (endpoint.scheme() != QStringLiteral("https")
        || endpoint.host().compare(QStringLiteral("api.groq.com"), Qt::CaseInsensitive) != 0
        || (endpoint.port(-1) != -1 && endpoint.port(-1) != 443))
        return {};
    if (model == QStringLiteral("canopylabs/orpheus-v1-english"))
        return {QStringLiteral("autumn"), QStringLiteral("diana"), QStringLiteral("hannah"),
            QStringLiteral("austin"), QStringLiteral("daniel"), QStringLiteral("troy")};
    if (model == QStringLiteral("canopylabs/orpheus-arabic-saudi"))
        return {QStringLiteral("abdullah"), QStringLiteral("fahad"), QStringLiteral("sultan"),
            QStringLiteral("lulwa"), QStringLiteral("noura"), QStringLiteral("aisha")};
    return {};
}

QStringList RemoteTts::availableVoices() const {
    return voicesFor(QUrl(m_endpoint), m_model);
}

QString RemoteTts::sanitizedProviderError(const QByteArray &payload, const int httpStatus) const {
    const QJsonDocument document = QJsonDocument::fromJson(payload.left(kMaximumProviderErrorBytes));
    if (!document.isObject()) return {};
    const QJsonObject root = document.object();
    QJsonObject errorObject;
    const QJsonValue errorValue = root.value(QStringLiteral("error"));
    if (errorValue.isObject()) errorObject = errorValue.toObject();
    const auto valueText = [](const QJsonValue &value) {
        return value.isString() ? value.toString() : QString();
    };
    QString code = valueText(errorObject.value(QStringLiteral("code")));
    if (code.isEmpty()) code = valueText(root.value(QStringLiteral("code")));
    const auto sanitizeCode = [](QString value) {
        value.replace(QRegularExpression(QStringLiteral("[\\x00-\\x1F\\x7F]")), QStringLiteral(" "));
        value.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
        return value.trimmed().left(128);
    };
    code = sanitizeCode(code);
    if (code == QStringLiteral("model_terms_required") && !availableVoices().isEmpty()) {
        const QString termsUrl = QStringLiteral("https://console.groq.com/playground?model=")
            + QString::fromLatin1(QUrl::toPercentEncoding(m_model));
        return QStringLiteral("HTTP %1 (model_terms_required): The organization administrator must accept the selected model's terms at %2.")
            .arg(httpStatus).arg(termsUrl);
    }
    const QHash<QString, QString> recoveries{
        {QStringLiteral("model_not_found"), QStringLiteral("The selected speech model was not found. Refresh the model list and choose an available model.")},
        {QStringLiteral("invalid_api_key"), QStringLiteral("The speech output API key was rejected. Choose or enter a valid key.")},
        {QStringLiteral("insufficient_quota"), QStringLiteral("The speech provider account has insufficient quota. Check its billing or spend limit.")},
        {QStringLiteral("rate_limit_exceeded"), QStringLiteral("The speech provider rate limit was reached. Wait and try again later.")},
        {QStringLiteral("blocked_api_access"), QStringLiteral("Speech API access is blocked by the provider account's spend or access limit.")},
        {QStringLiteral("model_permission_blocked"), QStringLiteral("The provider account is not permitted to use this speech model.")},
        {QStringLiteral("authentication_error"), QStringLiteral("The speech output API key was rejected. Choose or enter a valid key.")},
    };
    const auto recovery = recoveries.constFind(code);
    return recovery == recoveries.cend() ? QString() : QStringLiteral("HTTP %1 (%2): %3").arg(httpStatus).arg(code, recovery.value());
}

bool RemoteTts::ready() const {
    const QStringList voices = availableVoices();
    return m_secret->hasKey() && !m_model.isEmpty() && !m_voice.isEmpty()
        && (voices.isEmpty() || voices.contains(m_voice)) && validEndpoint(QUrl(m_endpoint));
}

QString RemoteTts::status() const {
    if (!validEndpoint(QUrl(m_endpoint))) return QStringLiteral("Choose an HTTPS endpoint or localhost HTTP endpoint");
    if (m_model.isEmpty()) return QStringLiteral("Enter a remote speech model identifier");
    if (m_voice.isEmpty()) return QStringLiteral("Enter a remote voice identifier");
    const QStringList voices = availableVoices();
    if (!voices.isEmpty() && !voices.contains(m_voice))
        return QStringLiteral("Choose a voice supported by the selected Groq speech model");
    return m_secret->hasKey() ? QStringLiteral("Remote speech output is ready") : QStringLiteral("Add a speech output API key");
}

bool RemoteTts::hasApiKey() const { return m_secret->hasKey(); }
QString RemoteTts::keyStorageStatus() const { return m_secret->storageStatus(); }
QString RemoteTts::keyStorageState() const { return m_secret->storageState(); }

void RemoteTts::setEndpoint(const QString &endpoint) {
    const QString value = endpoint.trimmed();
    if (value == m_endpoint) return;
    m_endpoint = value;
    const QStringList voices = availableVoices();
    if (!voices.isEmpty() && !voices.contains(m_voice))
        m_voice = defaultVoiceFor(QUrl(m_endpoint), m_model);
    m_secret->setEndpoint(m_endpoint);
    cancel();
    emit configurationChanged();
    emit availableVoicesChanged();
    emit readinessChanged();
}

void RemoteTts::setModel(const QString &model) {
    const QString value = model.trimmed().left(128);
    if (value == m_model) return;
    m_model = value;
    const QStringList voices = availableVoices();
    if (!voices.isEmpty() && !voices.contains(m_voice))
        m_voice = defaultVoiceFor(QUrl(m_endpoint), m_model);
    cancel();
    emit configurationChanged();
    emit availableVoicesChanged();
    emit readinessChanged();
}

void RemoteTts::setVoice(const QString &voice) {
    const QString value = voice.trimmed().left(128);
    if (value == m_voice) return;
    m_voice = value;
    cancel();
    emit configurationChanged();
    emit readinessChanged();
}

void RemoteTts::setApiKey(const QString &key) {
    if (key.trimmed().isEmpty()) m_secret->clearKey();
    else m_secret->saveKey(key);
}
void RemoteTts::clearApiKey() { m_secret->clearKey(); }

QList<struct RemoteTts::Chunk> RemoteTts::splitText(const QString &text) {
    QList<struct Chunk> chunks;
    QString remaining = text.trimmed();
    while (!remaining.isEmpty()) {
        qsizetype end = std::min<qsizetype>(200, remaining.size());
        if (end < remaining.size()) {
            const qsizetype sentence = remaining.lastIndexOf(QRegularExpression(QStringLiteral("[.!?](?:\\s|$)")), end);
            if (sentence > 80 && sentence + 1 <= 200) end = sentence + 1;
            else {
                const qsizetype space = remaining.lastIndexOf(QLatin1Char(' '), end);
                if (space > 80) end = space;
            }
        }
        if (end < remaining.size() && remaining.at(end).isLowSurrogate() && remaining.at(end - 1).isHighSurrogate()) --end;
        if (end <= 0) end = std::min<qsizetype>(200, remaining.size());
        const struct Chunk chunk{remaining.left(end).trimmed()};
        chunks.append(chunk);
        remaining = remaining.mid(end).trimmed();
    }
    return chunks;
}

bool RemoteTts::decodeWav(const QByteArray &payload, QByteArray *const pcm, QAudioFormat *const format,
    double *const seconds, QString *const error) {
    Q_ASSERT(pcm && format && seconds && error);
    if (payload.size() < 12 || payload.left(4) != QByteArrayLiteral("RIFF") || payload.mid(8, 4) != QByteArrayLiteral("WAVE")) {
        *error = QStringLiteral("The remote speech service returned a response that is not a RIFF WAV file."); return false;
    }
    const quint32 declaredSize = readLe32(payload.constData() + 4);
    const bool unknownRiffSize = declaredSize == kUnknownWaveSize;
    if ((!unknownRiffSize && declaredSize < 4)
        || (!unknownRiffSize && static_cast<quint64>(declaredSize) + 8 != static_cast<quint64>(payload.size()))) {
        *error = QStringLiteral("The remote speech WAV declares a truncated RIFF payload."); return false;
    }
    quint16 channels = 0, bits = 0, audioFormat = 0, blockAlign = 0; quint32 rate = 0, byteRate = 0; QByteArray data;
    const qsizetype riffEnd = unknownRiffSize ? payload.size() : static_cast<qsizetype>(declaredSize) + 8;
    qsizetype offset = 12;
    while (offset + 8 <= riffEnd) {
        const QByteArray id = payload.mid(offset, 4);
        const quint32 rawSize = readLe32(payload.constData() + offset + 4);
        offset += 8;
        const qsizetype remaining = riffEnd - offset;
        if (rawSize == kUnknownWaveSize) {
            if (!unknownRiffSize || id != QByteArrayLiteral("data")) {
                *error = QStringLiteral("The remote speech WAV contains an unsupported unknown-length chunk."); return false;
            }
            data = payload.mid(offset, remaining);
            offset = riffEnd;
            continue;
        }
        if (static_cast<quint64>(rawSize) > static_cast<quint64>(remaining)
            || static_cast<quint64>(rawSize) + (rawSize & 1U) > static_cast<quint64>(remaining)) {
            *error = QStringLiteral("The remote speech WAV contains a truncated chunk."); return false;
        }
        const qsizetype size = static_cast<qsizetype>(rawSize);
        if (id == QByteArrayLiteral("fmt ")) {
            if (size < 16) { *error = QStringLiteral("The remote speech WAV format chunk is incomplete."); return false; }
            const char *const p = payload.constData() + offset;
            audioFormat = readLe16(p); channels = readLe16(p + 2); rate = readLe32(p + 4); byteRate = readLe32(p + 8); blockAlign = readLe16(p + 12); bits = readLe16(p + 14);
        } else if (id == QByteArrayLiteral("data")) data = payload.mid(offset, size);
        offset += size + (size & 1);
    }
    if (offset != riffEnd) { *error = QStringLiteral("The remote speech WAV has an incomplete chunk header."); return false; }
    if (audioFormat != 1 || channels == 0 || channels > 8 || rate < 8000 || rate > 192000
        || (bits != 8 && bits != 16 && bits != 24 && bits != 32)
        || blockAlign != static_cast<quint16>(channels * (bits / 8))
        || byteRate != rate * blockAlign || data.isEmpty()) {
        *error = QStringLiteral("The remote speech WAV uses an unsupported PCM format."); return false;
    }
    const qsizetype sourceBytes = bits / 8;
    const qsizetype frameBytes = sourceBytes * channels;
    if (frameBytes <= 0 || data.size() % frameBytes != 0) { *error = QStringLiteral("The remote speech WAV has incomplete audio frames."); return false; }
    pcm->resize((data.size() / frameBytes) * channels * 2);
    char *destination = pcm->data();
    bool nonSilent = false;
    for (qsizetype offsetFrame = 0; offsetFrame < data.size(); offsetFrame += frameBytes) {
        for (quint16 channel = 0; channel < channels; ++channel) {
            const qint16 sample = sampleToInt16(data.constData() + offsetFrame + channel * sourceBytes, bits);
            nonSilent = nonSilent || sample != 0;
            std::memcpy(destination, &sample, sizeof(sample)); destination += sizeof(sample);
        }
    }
    format->setSampleRate(static_cast<int>(rate)); format->setChannelCount(channels); format->setSampleFormat(QAudioFormat::Int16);
    *seconds = static_cast<double>(pcm->size()) / static_cast<double>(format->bytesPerFrame() * format->sampleRate());
    if (*seconds <= 0.0 || *seconds > 90.0 || !nonSilent) {
        *error = !nonSilent ? QStringLiteral("The remote speech service returned silent audio.")
            : QStringLiteral("The remote speech response is longer than the supported speech limit.");
        return false;
    }
    return true;
}

void RemoteTts::synthesize(const QString &text, const QAudioFormat &preferredFormat, const quint64 request) {
    cancel();
    if (!ready()) { emit failed(status(), request); return; }
    if (text.trimmed().isEmpty() || text.size() > kMaximumTextCharacters) { emit failed(QStringLiteral("The speech prompt is empty or exceeds the remote speech limit of 8192 characters."), request); return; }
    if (!preferredFormat.isValid() || preferredFormat.channelCount() < 1 || preferredFormat.channelCount() > 8
        || preferredFormat.sampleRate() < 8000 || preferredFormat.sampleRate() > 192000
        || preferredFormat.bytesPerFrame() <= 0
        || (preferredFormat.sampleFormat() != QAudioFormat::UInt8 && preferredFormat.sampleFormat() != QAudioFormat::Int16
            && preferredFormat.sampleFormat() != QAudioFormat::Int32 && preferredFormat.sampleFormat() != QAudioFormat::Float)) {
        emit failed(QStringLiteral("The default audio output has no supported PCM format."), request); return;
    }
    m_request = request; m_preferredFormat = preferredFormat; m_chunks = splitText(text); m_chunkIndex = 0;
    m_totalSeconds = 0.0; m_assembledPcm.clear(); ++m_generation; m_overallTimeout->start(kOverallTimeoutMs); requestNext();
}

void RemoteTts::requestNext() {
    if (m_chunkIndex >= m_chunks.size()) return;
    const struct Chunk chunk = m_chunks.at(m_chunkIndex);
    QNetworkRequest request{QUrl(m_endpoint)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_secret->key().toUtf8());
    const QJsonObject body{{QStringLiteral("model"), m_model}, {QStringLiteral("voice"), m_voice},
        {QStringLiteral("input"), chunk.text}, {QStringLiteral("response_format"), QStringLiteral("wav")}};
    m_responseTooLarge = false;
    m_reply = m_network->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    m_reply->setReadBufferSize(kMaximumResponseBytes + 1);
    const quint64 generation = m_generation;
    connect(m_reply, &QNetworkReply::readyRead, this, [this] {
        if (!m_reply) return;
        const qsizetype remaining = kMaximumResponseBytes - m_payload.size();
        if (remaining <= 0) { m_responseTooLarge = true; m_reply->abort(); return; }
        m_payload.append(m_reply->read(remaining + 1));
        if (m_payload.size() > kMaximumResponseBytes) { m_responseTooLarge = true; m_reply->abort(); }
    });
    connect(m_reply, &QNetworkReply::finished, this, [this, generation] {
        if (!m_reply || generation != m_generation) return;
        QNetworkReply *const reply = m_reply;
        const qsizetype remaining = kMaximumResponseBytes - m_payload.size();
        if (remaining <= 0) { m_responseTooLarge = true; reply->abort(); handleReply(reply, m_payload, {}, false, generation); return; }
        const QByteArray tail = reply->read(remaining + 1);
        if (m_payload.size() <= kMaximumResponseBytes - tail.size()) m_payload.append(tail);
        else m_payload.resize(kMaximumResponseBytes + 1);
        const QString error = reply->error() == QNetworkReply::NoError ? QString() : reply->errorString();
        const bool redirected = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).isValid();
        handleReply(reply, m_payload, error, redirected, generation);
    });
    m_payload.clear(); m_timeout->start(kRequestTimeoutMs);
}

void RemoteTts::handleReply(QNetworkReply *const reply, const QByteArray &payload, const QString &networkError,
    const bool redirected, const quint64 generation) {
    m_timeout->stop(); m_reply = nullptr;
    if (generation != m_generation) return;
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();
    if (redirected) {
        m_overallTimeout->stop();
        emit failed(QStringLiteral("The remote speech service redirected the request. Check the configured endpoint."), m_request);
        return;
    }
    if (m_responseTooLarge) {
        m_overallTimeout->stop();
        emit failed(QStringLiteral("The remote speech response is too large."), m_request);
        return;
    }
    if (httpStatus >= 400) {
        const QString providerError = sanitizedProviderError(payload, httpStatus);
        const QString recovery = httpStatus == 401 || httpStatus == 403
            ? QStringLiteral("Check the speech output API key and whether the account can use this model.")
            : httpStatus == 429
            ? QStringLiteral("Check the provider rate limit or spend limit, then try again.")
            : QStringLiteral("Check the selected model, voice, and API key.");
        m_overallTimeout->stop();
        emit failed(providerError.isEmpty()
                ? QStringLiteral("Remote speech service rejected the request (HTTP %1). %2").arg(httpStatus).arg(recovery)
                : QStringLiteral("Remote speech service rejected the request: %1").arg(providerError), m_request);
        return;
    }
    if (!networkError.isEmpty()) {
        m_overallTimeout->stop();
        emit failed(QStringLiteral("Remote speech output could not reach the provider. Check the network connection and configured endpoint."), m_request);
        return;
    }
    if (payload.size() > kMaximumResponseBytes) {
        m_overallTimeout->stop();
        emit failed(QStringLiteral("The remote speech response is too large."), m_request);
        return;
    }
    const quint64 requestId = m_request;
    auto *const watcher = new QFutureWatcher<struct DecodedAudio>(this);
    m_decodeWatcher = watcher;
    connect(watcher, &QFutureWatcher<struct DecodedAudio>::finished, this, [this, watcher, generation, requestId] {
        if (m_decodeWatcher == watcher) m_decodeWatcher = nullptr;
        const struct DecodedAudio decoded = watcher->result();
        watcher->deleteLater();
        if (generation != m_generation || requestId != m_request) return;
        if (!decoded.valid) { emit failed(decoded.error, m_request); return; }
        if (decoded.pcm.isEmpty() || m_assembledPcm.size() > 64 * 1024 * 1024 - decoded.pcm.size()) {
            emit failed(QStringLiteral("The remote speech response exceeds the supported audio limit or format conversion failed."), m_request); return;
        }
        m_totalSeconds += decoded.seconds;
        if (m_totalSeconds > 90.0) { emit failed(QStringLiteral("The remote speech request exceeds the 90 second playback limit."), m_request); return; }
        m_assembledPcm.append(decoded.pcm);
        ++m_chunkIndex;
        if (m_chunkIndex < m_chunks.size()) { requestNext(); return; }
        emit audioReady(m_assembledPcm, m_preferredFormat, m_totalSeconds, m_request);
        m_overallTimeout->stop();
    });
    const QByteArray payloadCopy = payload;
    const QAudioFormat preferredFormat = m_preferredFormat;
    watcher->setFuture(QtConcurrent::run([payloadCopy, preferredFormat] {
        struct DecodedAudio decoded;
        decoded.valid = RemoteTts::decodeWav(payloadCopy, &decoded.pcm, &decoded.format, &decoded.seconds, &decoded.error);
        if (decoded.valid) {
            const QByteArray converted = convertPcm(decoded.pcm, decoded.format, preferredFormat);
            if (converted.isEmpty()) { decoded.valid = false; decoded.error = QStringLiteral("The remote speech format could not be converted for the selected audio output."); }
            else { decoded.pcm = converted; decoded.format = preferredFormat; }
        }
        return decoded;
    }));
}

void RemoteTts::cancelReply() {
    m_timeout->stop();
    if (m_reply) { m_reply->disconnect(this); m_reply->abort(); m_reply->deleteLater(); m_reply = nullptr; }
    m_payload.clear(); m_responseTooLarge = false;
}

void RemoteTts::cancel() {
    ++m_generation; m_overallTimeout->stop(); cancelReply(); m_chunks.clear(); m_chunkIndex = 0; m_request = 0;
    m_totalSeconds = 0.0; m_assembledPcm.clear();
}
