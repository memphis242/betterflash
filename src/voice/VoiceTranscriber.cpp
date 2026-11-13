#include "VoiceTranscriber.h"

#include "VoiceAudio.h"
#include "core/DiagnosticLog.h"

#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

VoiceTranscriber::VoiceTranscriber(QString apiKey, QString model, QObject *parent,
    QNetworkAccessManager *const network)
    : QObject(parent), m_apiKey(std::move(apiKey)), m_model(std::move(model)),
      m_network(network ? network : new QNetworkAccessManager(this)) {}

VoiceTranscriber::~VoiceTranscriber() { cancel(); }

QNetworkRequest VoiceTranscriber::request(const QString &apiKey) {
    QNetworkRequest request(QUrl(QStringLiteral("https://api.groq.com/openai/v1/audio/transcriptions")));
    request.setRawHeader(QByteArrayLiteral("Authorization"), QByteArrayLiteral("Bearer ") + apiKey.toUtf8());
    request.setRawHeader(QByteArrayLiteral("User-Agent"), QByteArrayLiteral("BetterFlash/0.1"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(30000);
    return request;
}

void VoiceTranscriber::submit(const QByteArrayView pcm, const quint64 epoch) {
    Q_ASSERT(!pcm.isEmpty() && pcm.size() % 2 == 0 && pcm.size() <= VoiceUtteranceBuffer::MaxBytes);
    cancel();
    const quint64 generation = m_generation;
    auto *const multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart file;
    file.setHeader(QNetworkRequest::ContentDispositionHeader,
        QStringLiteral("form-data; name=\"file\"; filename=\"utterance.wav\""));
    file.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("audio/wav"));
    file.setBody(voiceWav(pcm));
    multi->append(file);
    QHttpPart model;
    model.setHeader(QNetworkRequest::ContentDispositionHeader, QStringLiteral("form-data; name=\"model\""));
    model.setBody(m_model.toUtf8());
    multi->append(model);
    QHttpPart responseFormat;
    responseFormat.setHeader(QNetworkRequest::ContentDispositionHeader, QStringLiteral("form-data; name=\"response_format\""));
    responseFormat.setBody(QByteArrayLiteral("json"));
    multi->append(responseFormat);
    QNetworkReply *const reply = m_network->post(request(m_apiKey), multi);
    m_reply = reply;
    multi->setParent(reply);
    connect(reply, &QNetworkReply::readyRead, this, [reply] {
        if (reply->bytesAvailable() > 65536) {
            reply->setProperty("responseTooLarge", true);
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, epoch, generation] {
        reply->deleteLater();
        if (generation != m_generation || m_reply != reply) return;
        m_reply = nullptr;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool responseTooLarge = reply->property("responseTooLarge").toBool() || reply->bytesAvailable() > 65536;
        QString detail = QStringLiteral("Endpoint: https://api.groq.com/openai/v1/audio/transcriptions\nHTTP status: %1\nNetwork error %2: %3")
            .arg(status ? QString::number(status) : QStringLiteral("not received"))
            .arg(static_cast<int>(reply->error())).arg(reply->errorString());
        if (status < 200 || status >= 300) {
            const QByteArray body = reply->read(16385);
            if (!body.isEmpty()) detail += QStringLiteral("\nResponse body: ") + QString::fromUtf8(body.left(16384))
                + (body.size() > 16384 || reply->bytesAvailable() > 0 ? QStringLiteral("\n[Response truncated]") : QString());
        }
        if (!m_apiKey.isEmpty()) detail.replace(m_apiKey, QStringLiteral("[redacted]"));
        if (const auto *const log = DiagnosticLog::instance()) detail = log->redact(detail);
        if (responseTooLarge) {
            emit errorOccurred(QStringLiteral("VOICE_RESPONSE_TOO_LARGE"), QStringLiteral("Groq returned an oversized response. Try voice again."), false, epoch, detail);
        } else if (status >= 300 && status < 400) {
            emit errorOccurred(QStringLiteral("VOICE_REDIRECT_BLOCKED"), QStringLiteral("The speech endpoint redirected the request. Update BetterFlash before retrying."), false, epoch, detail);
        } else if (status == 401 || status == 403) {
            emit errorOccurred(QStringLiteral("GROQ_KEY_REJECTED"), QStringLiteral("Groq rejected the API key. Replace it in Voice settings and enable voice again."), true, epoch, detail);
        } else if (status == 429) {
            emit errorOccurred(QStringLiteral("GROQ_RATE_LIMITED"), QStringLiteral("Groq is rate limiting speech requests. Wait and retry, or check your account limits."), false, epoch, detail);
        } else if (reply->error() == QNetworkReply::TimeoutError || reply->error() == QNetworkReply::OperationCanceledError) {
            emit errorOccurred(QStringLiteral("VOICE_REQUEST_TIMEOUT"), QStringLiteral("Speech transcription timed out. Check your connection and try again."), false, epoch, detail);
        } else if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
            emit errorOccurred(QStringLiteral("VOICE_NETWORK_ERROR"), QStringLiteral("Groq transcription failed. Check your connection, model access, and account status, then try again."), false, epoch, detail);
        } else {
            struct QJsonParseError parseError{};
            const QByteArray body = reply->readAll();
            const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
            const QJsonObject object = document.object();
            if (parseError.error != QJsonParseError::NoError || !document.isObject() || !object.value(QStringLiteral("text")).isString()) {
                QString responseDetail = detail + QStringLiteral("\nJSON parse error: %1\nResponse body: ").arg(parseError.errorString())
                    + QString::fromUtf8(body.left(16384))
                    + (body.size() > 16384 ? QStringLiteral("\n[Response truncated]") : QString());
                if (!m_apiKey.isEmpty()) responseDetail.replace(m_apiKey, QStringLiteral("[redacted]"));
                if (const auto *const log = DiagnosticLog::instance()) responseDetail = log->redact(responseDetail);
                emit errorOccurred(QStringLiteral("VOICE_TRANSCRIPT_INVALID"), QStringLiteral("Groq returned an unreadable transcript. Try the utterance again."), false, epoch, responseDetail);
            } else {
                const QString text = object.value(QStringLiteral("text")).toString().trimmed();
                if (!text.isEmpty()) emit transcriptReady(text, epoch);
            }
        }
        if (generation == m_generation) emit finished(epoch);
    });
}

void VoiceTranscriber::cancel() {
    ++m_generation;
    if (!m_reply) return;
    QNetworkReply *const reply = m_reply;
    m_reply = nullptr;
    reply->abort();
    reply->deleteLater();
}
