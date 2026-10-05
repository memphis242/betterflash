#pragma once

#include <QObject>
#include <QByteArrayView>
#include <QPointer>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;

// The endpoint is pinned; a supplied manager lets tests exercise replies without HTTP.
class VoiceTranscriber final : public QObject {
    Q_OBJECT
public:
    explicit VoiceTranscriber(QString apiKey, QString model, QObject *parent = nullptr,
        QNetworkAccessManager *network = nullptr);
    ~VoiceTranscriber() override;
    [[nodiscard]] static QNetworkRequest request(const QString &apiKey);
    [[nodiscard]] bool busy() const { return !m_reply.isNull(); }
    void submit(QByteArrayView pcm, quint64 epoch);
    void cancel();
signals:
    void transcriptReady(const QString &text, quint64 epoch);
    void errorOccurred(const QString &code, const QString &message, bool fatal, quint64 epoch);
    void finished(quint64 epoch);
private:
    const QString m_apiKey;
    const QString m_model;
    QNetworkAccessManager *const m_network;
    QPointer<QNetworkReply> m_reply;
    quint64 m_generation = 0;
};
