#pragma once

#include <QAudioFormat>
#include <QByteArray>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QList>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
template<typename T> class QFutureWatcher;
class QTimer;
class SecretStore;

class RemoteTts final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString endpoint READ endpoint WRITE setEndpoint NOTIFY configurationChanged)
    Q_PROPERTY(QString model READ model WRITE setModel NOTIFY configurationChanged)
    Q_PROPERTY(QString voice READ voice WRITE setVoice NOTIFY configurationChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readinessChanged)
    Q_PROPERTY(QString status READ status NOTIFY readinessChanged)
    Q_PROPERTY(bool hasApiKey READ hasApiKey NOTIFY keyChanged)
    Q_PROPERTY(QString keyStorageStatus READ keyStorageStatus NOTIFY keyChanged)
    Q_PROPERTY(QString keyStorageState READ keyStorageState NOTIFY keyChanged)
public:
    explicit RemoteTts(QObject *parent = nullptr);
    ~RemoteTts() override;

    [[nodiscard]] QString endpoint() const { return m_endpoint; }
    [[nodiscard]] QString model() const { return m_model; }
    [[nodiscard]] QString voice() const { return m_voice; }
    [[nodiscard]] bool ready() const;
    [[nodiscard]] QString status() const;
    [[nodiscard]] bool hasApiKey() const;
    [[nodiscard]] QString keyStorageStatus() const;
    [[nodiscard]] QString keyStorageState() const;

    void setEndpoint(const QString &endpoint);
    void setModel(const QString &model);
    void setVoice(const QString &voice);
    Q_INVOKABLE void setApiKey(const QString &key);
    Q_INVOKABLE void clearApiKey();

    void synthesize(const QString &text, const QAudioFormat &preferredFormat, quint64 request);
    void cancel();

signals:
    void configurationChanged();
    void readinessChanged();
    void keyChanged();
    void credentialChanged();
    void audioReady(const QByteArray &pcm, const QAudioFormat &format, double seconds,
        quint64 request);
    void failed(const QString &message, quint64 request);

private:
    struct Chunk { QString text; };
    void requestNext();
    void cancelReply();
    void handleReply(QNetworkReply *reply, const QByteArray &payload, const QString &error,
        bool redirected, quint64 generation);
    static QList<struct Chunk> splitText(const QString &text);
    static bool decodeWav(const QByteArray &payload, QByteArray *pcm, QAudioFormat *format,
        double *seconds, QString *error);
    static bool validEndpoint(const QUrl &url);

    QString m_endpoint = QStringLiteral("https://api.groq.com/openai/v1/audio/speech");
    QString m_model = QStringLiteral("canopylabs/orpheus-v1-english");
    QString m_voice = QStringLiteral("troy");
    SecretStore *const m_secret;
    QNetworkAccessManager *const m_network;
    QPointer<QNetworkReply> m_reply;
    QTimer *const m_timeout;
    QList<struct Chunk> m_chunks;
    qsizetype m_chunkIndex = 0;
    quint64 m_generation = 0;
    quint64 m_request = 0;
    QByteArray m_payload;
    bool m_responseTooLarge = false;
    QAudioFormat m_preferredFormat;
    QByteArray m_assembledPcm;
    struct DecodedAudio {
        QByteArray pcm;
        QAudioFormat format;
        double seconds = 0.0;
        QString error;
        bool valid = false;
    };
    QFutureWatcher<struct DecodedAudio> *m_decodeWatcher = nullptr;
    double m_totalSeconds = 0.0;
    QTimer *const m_overallTimeout;
    QString m_lastKey;
};
