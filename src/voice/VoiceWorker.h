#pragma once

#include "VoiceAudio.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariant>

class QAudioSource;
class QIODevice;
class QLibrary;
class QNetworkRequest;
class VoiceTranscriber;

class VoiceWorker final : public QObject {
    Q_OBJECT
public:
    struct ParsedUtterance {
        bool isCommand = false;
        QString command;
        QVariant argument;
    };

    explicit VoiceWorker(QString modelPath, QObject *parent = nullptr);
    ~VoiceWorker() override;

    [[nodiscard]] static struct ParsedUtterance parseUtterance(const QString &utterance);
    [[nodiscard]] static QNetworkRequest groqRequest(const QString &apiKey);
    void setApiKey(const QString &key) { m_apiKey = key; }
    void setProvider(const QString &provider) { m_provider = provider; }
    void setSttModel(const QString &model) { m_sttModel = model; }

public slots:
    void initialize();
    void setListening(bool listening, quint64 epoch);
    void shutdown();

signals:
    void ready();
    void listeningChanged(bool listening, quint64 epoch);
    void processingChanged(bool processing, quint64 epoch);
    void transcriptReady(const QString &transcript, quint64 epoch);
    void errorOccurred(const QString &code, const QString &message, bool fatal);

private:
    bool loadLibrary();
    bool prepareAudio();
    void startCapture();
    void stopCapture();
    void readAudio();
    void submitRecording(const QByteArray &pcm);
    void cancelRequest();
    void resumeCapture(quint64 epoch);
    void processLocalResult(const QByteArray &json);
    void reportError(const QString &code, const QString &message, bool fatal);

    QString m_modelPath;
    QString m_apiKey;
    QString m_provider = QStringLiteral("groq");
    QString m_sttModel = QStringLiteral("whisper-large-v3-turbo");
    void *m_model = nullptr;
    void *m_recognizer = nullptr;
    QAudioSource *m_audio = nullptr;
    QIODevice *m_audioDevice = nullptr;
    QLibrary *m_library = nullptr;
    VoiceTranscriber *m_transcriber = nullptr;
    VoicePcmConverter m_converter;
    VoiceUtteranceBuffer m_utterance;
    quint64 m_captureEpoch = 0;
    bool m_initialized = false;
    bool m_requestedListening = false;
    bool m_listening = false;
    bool m_processing = false;
    bool m_shuttingDown = false;

    using ModelNew = void *(*)(const char *);
    using ModelFree = void (*)(void *);
    using RecognizerNew = void *(*)(void *, float);
    using RecognizerFree = void (*)(void *);
    using RecognizerReset = void (*)(void *);
    using AcceptWaveform = int (*)(void *, const char *, int);
    using Result = const char *(*)(void *);
    ModelNew m_modelNew = nullptr;
    ModelFree m_modelFree = nullptr;
    RecognizerNew m_recognizerNew = nullptr;
    RecognizerFree m_recognizerFree = nullptr;
    RecognizerReset m_recognizerReset = nullptr;
    AcceptWaveform m_acceptWaveform = nullptr;
    Result m_result = nullptr;
};
