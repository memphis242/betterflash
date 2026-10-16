#pragma once

#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVector>
#include <QUrl>
#include "VoiceAudio.h"

class QAudioSource;
class QIODevice;
class QThread;
class QTimer;
class VoiceWorker;
class TranscriptCorrector;

class CustomSttTests final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY stateChanged)
    Q_PROPERTY(bool recordingPaused READ recordingPaused NOTIFY recordingMetricsChanged)
    Q_PROPERTY(double recordingDuration READ recordingDuration NOTIFY recordingMetricsChanged)
    Q_PROPERTY(double recordingLevel READ recordingLevel NOTIFY recordingMetricsChanged)
    Q_PROPERTY(QVariantList recordingActivity READ recordingActivity NOTIFY recordingActivityChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(bool canRun READ canRun NOTIFY stateChanged)
    Q_PROPERTY(QString currentRecordingId READ currentRecordingId NOTIFY stateChanged)
public:
    explicit CustomSttTests(QObject *parent = nullptr);
    ~CustomSttTests() override;

    [[nodiscard]] QVariantList entries() const;
    [[nodiscard]] bool busy() const { return m_busy; }
    [[nodiscard]] bool recording() const { return m_recording; }
    [[nodiscard]] bool recordingPaused() const { return m_recordingPaused; }
    [[nodiscard]] double recordingDuration() const { return static_cast<double>(m_recordedPcm.size()) / (VoiceUtteranceBuffer::SampleRate * 2.0); }
    [[nodiscard]] double recordingLevel() const { return m_recordingLevel; }
    [[nodiscard]] QVariantList recordingActivity() const { return m_recordingActivity; }
    [[nodiscard]] QString status() const { return m_status; }
    [[nodiscard]] QString error() const { return m_error; }
    [[nodiscard]] bool canRun() const;
    [[nodiscard]] QString currentRecordingId() const { return m_recordingId; }

    void setConfiguration(QString provider, QString modelPath, QString model, QString apiKey, QString vocabulary = {}, QString acceleration = QStringLiteral("automatic"));
    void setTranscriptCorrector(TranscriptCorrector *corrector);

    Q_INVOKABLE void addRecording(const QUrl &source);
    Q_INVOKABLE void replaceRecording(const QString &id, const QUrl &source);
    Q_INVOKABLE bool setExpected(const QString &id, const QString &text);
    Q_INVOKABLE void setLabel(const QString &id, const QString &text);
    Q_INVOKABLE void removeRecording(const QString &id);
    Q_INVOKABLE void runTests();
    Q_INVOKABLE void runTest(const QString &id);
    Q_INVOKABLE void cancelTests();
    void setInteractionBlocked(bool blocked) { m_interactionBlocked = blocked; }
    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void startRecordingForSlot(const QString &id);
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void pauseRecording();
    Q_INVOKABLE void resumeRecording();

signals:
    void entriesChanged();
    void stateChanged();
    void recordingMetricsChanged();
    void recordingActivityChanged();

private:
    struct Entry {
        QString id;
        QString label;
        QString path;
        QString expected;
        QString transcript;
        QString rawTranscript;
        QString correctedTranscript;
        QString correctionStatus;
        QString correctionError;
        QString status = QStringLiteral("Need recording");
        QString error;
        double duration = 0.0;
        double wordErrorRate = -1.0;
        int substitutions = 0;
        int deletions = 0;
        int insertions = 0;
        double correctedWordErrorRate = -1.0;
        int correctedSubstitutions = 0;
        int correctedDeletions = 0;
        int correctedInsertions = 0;
        QString resultConfiguration;
        QString resultCredentialHash;
    };

    [[nodiscard]] struct Entry *find(const QString &id);
    [[nodiscard]] const struct Entry *find(const QString &id) const;
    void load();
    bool persist();
    void notifyEntries();
    void setState(bool busy, QString status, QString error = {});
    void resetRecordingMetrics();
    void resetRecordingSignal();
    void updateRecordingMetrics();
    void finishCurrent(quint64 generation, quint64 workerEpoch, const QString &id, const QString &transcript, const QString &workerError);
    void startNext(quint64 generation);
    void retireWorker();
    bool importWave(const QUrl &source, QString *path, double *duration, QString *error) const;
    static void score(const QString &expected, const QString &actual, int *substitutions,
        int *deletions, int *insertions, double *wer);

    QVector<struct Entry> m_entries;
    QVector<QString> m_queue;
    QPointer<TranscriptCorrector> m_corrector;
    QMetaObject::Connection m_correctorConfigurationConnection;
    QString m_directory;
    QString m_provider = QStringLiteral("local");
    QString m_modelPath;
    QString m_model = QStringLiteral("whisper-large-v3-turbo");
    QString m_apiKey;
    QString m_vocabulary;
    QString m_acceleration = QStringLiteral("automatic");
    bool m_busy = false;
    bool m_interactionBlocked = false;
    bool m_recording = false;
    bool m_recordingPaused = false;
    QString m_status = QStringLiteral("Add a recording to begin.");
    QString m_error;
    int m_current = -1;
    QString m_recordingId;
    quint64 m_generation = 0;
    quint64 m_workerGeneration = 0;
    quint64 m_nextWorkerGeneration = 0;
    quint64 m_correctionGeneration = 0;
    quint64 m_correctionJob = 0;
    QPointer<VoiceWorker> m_worker;
    QPointer<QThread> m_thread;
    QVector<QPointer<QThread>> m_threads;
    QAudioSource *m_audioSource = nullptr;
    QIODevice *m_audioDevice = nullptr;
    QByteArray m_recordedPcm;
    VoicePcmConverter m_converter;
    QPointer<QTimer> m_recordingMeterTimer;
    double m_recordingLevel = 0.0;
    QVariantList m_recordingActivity;
    double m_meterSquareSum = 0.0;
    qsizetype m_meterSampleCount = 0;
};
