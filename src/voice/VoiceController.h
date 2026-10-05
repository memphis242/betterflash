#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariant>
#include <QVector>

class SecretStore;
class QTextToSpeech;
class QThread;
class QTimer;
class VoiceWorker;

class VoiceController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(bool listening READ listening NOTIFY listeningChanged)
    Q_PROPERTY(bool speaking READ speaking NOTIFY speakingChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString lastTranscript READ lastTranscript NOTIFY lastTranscriptChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString modelPath READ modelPath WRITE setModelPath NOTIFY modelPathChanged)
    Q_PROPERTY(bool hasApiKey READ hasApiKey NOTIFY hasApiKeyChanged)
    Q_PROPERTY(QString sttProvider READ sttProvider WRITE setSttProvider NOTIFY sttProviderChanged)
    Q_PROPERTY(QString sttModel READ sttModel WRITE setSttModel NOTIFY sttModelChanged)
    Q_PROPERTY(QString keyStorageStatus READ keyStorageStatus NOTIFY keyStorageStatusChanged)
public:
    explicit VoiceController(QObject *parent = nullptr);
    ~VoiceController() override;

    [[nodiscard]] bool enabled() const { return m_enabled; }
    [[nodiscard]] bool ready() const { return m_ready; }
    [[nodiscard]] bool listening() const { return m_listening; }
    [[nodiscard]] bool speaking() const { return m_speaking; }
    [[nodiscard]] QString status() const { return m_status; }
    [[nodiscard]] QString lastTranscript() const { return m_lastTranscript; }
    [[nodiscard]] QString lastError() const { return m_lastError; }
    [[nodiscard]] QString modelPath() const { return m_modelPath; }
    [[nodiscard]] bool hasApiKey() const;
    [[nodiscard]] QString sttProvider() const { return m_sttProvider; }
    [[nodiscard]] QString sttModel() const { return m_sttModel; }
    [[nodiscard]] QString keyStorageStatus() const;

    Q_INVOKABLE void setEnabled(bool enabled);
    void setModelPath(const QString &path);
    void setSttProvider(const QString &provider);
    void setSttModel(const QString &model);
    Q_INVOKABLE void startListening();
    Q_INVOKABLE void stopListening();
    Q_INVOKABLE void speak(const QString &text);
    Q_INVOKABLE void stopSpeaking();
    Q_INVOKABLE void cancelPendingRecognition();
    Q_INVOKABLE void processTranscript(const QString &transcript);
    Q_INVOKABLE void setApiKey(const QString &key);
    Q_INVOKABLE void clearApiKey();

signals:
    void enabledChanged();
    void readyChanged();
    void listeningChanged();
    void speakingChanged();
    void statusChanged();
    void lastTranscriptChanged();
    void lastErrorChanged();
    void modelPathChanged();
    void hasApiKeyChanged();
    void sttProviderChanged();
    void sttModelChanged();
    void keyStorageStatusChanged();
    void commandRecognized(const QString &command, const QVariant &argument);
    void answerRecognized(const QString &answer);
    void utteranceFinished();
    void errorOccurred(const QString &code, const QString &message);

private:
    void beginWorker();
    void retireWorker();
    void restartWorker();
    void updateCapture();
    void beginSpeech();
    void finishSpeech();
    void speechError(const QString &message);
    void onSpeechStateChanged();
    void setReady(bool ready);
    void setListeningState(bool listening);
    void setSpeakingState(bool speaking);
    void setStatus(const QString &status);
    void setError(const QString &code, const QString &message);
    void persistConfiguration() const;

    bool m_enabled = false;
    bool m_ready = false;
    bool m_listening = false;
    bool m_speaking = false;
    bool m_wantsListening = false;
    bool m_processing = false;
    bool m_captureSuspended = true;
    bool m_ttsStarted = false;
    bool m_ttsIssued = false;
    bool m_stoppingSpeech = false;
    bool m_echoTail = false;
    quint64 m_workerGeneration = 0;
    quint64 m_captureEpoch = 0;
    quint64 m_speechGeneration = 0;
    QString m_status = QStringLiteral("Voice disabled");
    QString m_lastTranscript;
    QString m_lastError;
    QString m_modelPath;
    QString m_sttProvider = QStringLiteral("groq");
    QString m_sttModel = QStringLiteral("whisper-large-v3-turbo");
    QString m_pendingSpeech;
    QString m_lastKey;
    SecretStore *const m_secret;
    QTextToSpeech *m_speech = nullptr;
    QPointer<VoiceWorker> m_worker;
    QPointer<QThread> m_workerThread;
    QVector<QPointer<QThread>> m_threads;
};
