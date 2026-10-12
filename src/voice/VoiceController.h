#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariant>
#include <QVariantMap>
#include <QVector>
#include <QAudioFormat>
#include <atomic>
#include <memory>

class SecretStore;
class QTextToSpeech;
class QThread;
class QTimer;
class VoiceWorker;
class LocalTtsWorker;
class ModelDownload;
class QAudioSink;
class QBuffer;

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
    Q_PROPERTY(QString sttProfile READ sttProfile WRITE setSttProfile NOTIFY sttProfileChanged)
    Q_PROPERTY(bool sttModelReady READ sttModelReady NOTIFY modelReadinessChanged)
    Q_PROPERTY(bool sttModelDownloading READ sttModelDownloading NOTIFY modelReadinessChanged)
    Q_PROPERTY(QString sttModelStatus READ sttModelStatus NOTIFY modelReadinessChanged)
    Q_PROPERTY(double sttModelProgress READ sttModelProgress NOTIFY modelReadinessChanged)
    Q_PROPERTY(QString ttsProvider READ ttsProvider WRITE setTtsProvider NOTIFY ttsProviderChanged)
    Q_PROPERTY(QString ttsProfile READ ttsProfile WRITE setTtsProfile NOTIFY ttsProfileChanged)
    Q_PROPERTY(bool ttsReady READ ttsReady NOTIFY modelReadinessChanged)
    Q_PROPERTY(QString ttsStatus READ ttsStatus NOTIFY modelReadinessChanged)
    Q_PROPERTY(bool ttsModelDownloading READ ttsModelDownloading NOTIFY modelReadinessChanged)
    Q_PROPERTY(double ttsModelProgress READ ttsModelProgress NOTIFY modelReadinessChanged)
    Q_PROPERTY(bool sttTestBusy READ sttTestBusy NOTIFY modelTestChanged)
    Q_PROPERTY(QString sttTestStatus READ sttTestStatus NOTIFY modelTestChanged)
    Q_PROPERTY(QString sttTestError READ sttTestError NOTIFY modelTestChanged)
    Q_PROPERTY(QVariantMap sttTestReport READ sttTestReport NOTIFY modelTestChanged)
    Q_PROPERTY(bool ttsTestBusy READ ttsTestBusy NOTIFY modelTestChanged)
    Q_PROPERTY(QString ttsTestStatus READ ttsTestStatus NOTIFY modelTestChanged)
    Q_PROPERTY(QString ttsTestError READ ttsTestError NOTIFY modelTestChanged)
    Q_PROPERTY(QVariantMap ttsTestReport READ ttsTestReport NOTIFY modelTestChanged)
    Q_PROPERTY(bool modelTestBusy READ modelTestBusy NOTIFY modelTestChanged)
    Q_PROPERTY(QString modelTestStatus READ modelTestStatus NOTIFY modelTestChanged)
    Q_PROPERTY(QString modelTestError READ modelTestError NOTIFY modelTestChanged)
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
    [[nodiscard]] QString sttProfile() const { return m_sttProfile; }
    [[nodiscard]] bool sttModelReady() const;
    [[nodiscard]] bool sttModelDownloading() const;
    [[nodiscard]] double sttModelProgress() const;
    [[nodiscard]] QString sttModelStatus() const;
    [[nodiscard]] QString ttsProvider() const { return m_ttsProvider; }
    [[nodiscard]] QString ttsProfile() const { return m_ttsProfile; }
    [[nodiscard]] bool ttsReady() const;
    [[nodiscard]] QString ttsStatus() const;
    [[nodiscard]] bool ttsModelDownloading() const;
    [[nodiscard]] double ttsModelProgress() const;
    [[nodiscard]] bool sttTestBusy() const { return m_sttTestBusy; }
    [[nodiscard]] QString sttTestStatus() const { return m_sttTestStatus; }
    [[nodiscard]] QString sttTestError() const { return m_sttTestError; }
    [[nodiscard]] QVariantMap sttTestReport() const { return m_sttTestReport; }
    [[nodiscard]] bool ttsTestBusy() const { return m_ttsTestBusy; }
    [[nodiscard]] QString ttsTestStatus() const { return m_ttsTestStatus; }
    [[nodiscard]] QString ttsTestError() const { return m_ttsTestError; }
    [[nodiscard]] QVariantMap ttsTestReport() const { return m_ttsTestReport; }
    [[nodiscard]] bool modelTestBusy() const { return m_modelTestBusy; }
    [[nodiscard]] QString modelTestStatus() const { return m_modelTestStatus; }
    [[nodiscard]] QString modelTestError() const { return m_modelTestError; }
    [[nodiscard]] QString keyStorageStatus() const;

    Q_INVOKABLE void setEnabled(bool enabled);
    void setModelPath(const QString &path);
    void setSttProvider(const QString &provider);
    void setSttModel(const QString &model);
    void setSttProfile(const QString &profile);
    void setTtsProvider(const QString &provider);
    void setTtsProfile(const QString &profile);
    Q_INVOKABLE void testSttModel();
    Q_INVOKABLE void testTtsModel();
    Q_INVOKABLE void downloadSttModel();
    Q_INVOKABLE void downloadTtsModel();
    Q_INVOKABLE void cancelSttDownload();
    Q_INVOKABLE void cancelTtsDownload();
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
    void sttProfileChanged();
    void ttsProviderChanged();
    void ttsProfileChanged();
    void modelReadinessChanged();
    void modelTestChanged();
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
    void setModelTestState(bool busy, const QString &status, const QString &error = {});
    void configureDownloads(bool onlyTts = false);
    void continuePendingOperations();
    void beginSttTest();
    void finishSttTest(bool passed, const QString &message, const QString &error = {});
    void beginTtsTest();
    void finishTtsTest(bool passed, const QString &message, const QString &error = {});
    void resetSttTestReport();
    void resetTtsTestReport();
    void beginLocalSpeech();
    void retireTtsWorker();
    void startLocalPlayback(const QByteArray &pcm, quint64 request, double seconds);

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
    QString m_sttProvider = QStringLiteral("local");
    QString m_sttModel = QStringLiteral("whisper-large-v3-turbo");
    QString m_sttProfile = QStringLiteral("small-en");
    QString m_ttsProvider = QStringLiteral("local");
    QString m_ttsProfile = QStringLiteral("ljspeech-medium");
    bool m_modelTestBusy = false;
    QString m_modelTestStatus = QStringLiteral("Not tested");
    QString m_modelTestError;
    bool m_sttTestBusy = false;
    bool m_ttsTestBusy = false;
    bool m_pendingSttTest = false;
    bool m_pendingTtsTest = false;
    bool m_testSavedWantsListening = false;
    QString m_sttTestStatus = QStringLiteral("Not tested");
    QString m_sttTestError;
    QVariantMap m_sttTestReport;
    QString m_ttsTestStatus = QStringLiteral("Not tested");
    QString m_ttsTestError;
    QVariantMap m_ttsTestReport;
    QString m_localDirectory;
    ModelDownload *m_sttDownload = nullptr;
    ModelDownload *m_ttsDownload = nullptr;
    ModelDownload *m_fixtureDownload = nullptr;
    QPointer<VoiceWorker> m_testWorker;
    QPointer<QThread> m_testThread;
    QPointer<LocalTtsWorker> m_ttsWorker;
    QPointer<QThread> m_ttsThread;
    QPointer<QThread> m_retiringTtsThread;
    QAudioSink *m_audioSink = nullptr;
    QBuffer *m_playbackBuffer = nullptr;
    QAudioFormat m_playbackFormat;
    std::shared_ptr<std::atomic_bool> m_synthesisCancelled = std::make_shared<std::atomic_bool>(false);
    quint64 m_testGeneration = 0;
    QString m_pendingSpeech;
    QString m_lastKey;
    SecretStore *const m_secret;
    QTextToSpeech *m_speech = nullptr;
    QPointer<VoiceWorker> m_worker;
    QPointer<QThread> m_workerThread;
    QVector<QPointer<QThread>> m_threads;
};
