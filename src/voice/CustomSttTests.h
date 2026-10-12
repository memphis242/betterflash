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
class VoiceWorker;

class CustomSttTests final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(bool canRun READ canRun NOTIFY stateChanged)
public:
    explicit CustomSttTests(QObject *parent = nullptr);
    ~CustomSttTests() override;

    [[nodiscard]] QVariantList entries() const;
    [[nodiscard]] bool busy() const { return m_busy; }
    [[nodiscard]] bool recording() const { return m_recording; }
    [[nodiscard]] QString status() const { return m_status; }
    [[nodiscard]] QString error() const { return m_error; }
    [[nodiscard]] bool canRun() const;

    void setConfiguration(QString provider, QString modelPath, QString model, QString apiKey);

    Q_INVOKABLE void addRecording(const QUrl &source);
    Q_INVOKABLE void setExpected(const QString &id, const QString &text);
    Q_INVOKABLE void setLabel(const QString &id, const QString &text);
    Q_INVOKABLE void removeRecording(const QString &id);
    Q_INVOKABLE void runTests();
    Q_INVOKABLE void cancelTests();
    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void stopRecording();

signals:
    void entriesChanged();
    void stateChanged();

private:
    struct Entry {
        QString id;
        QString label;
        QString path;
        QString expected;
        QString transcript;
        QString status = QStringLiteral("Ready");
        QString error;
        double duration = 0.0;
        double wordErrorRate = -1.0;
        int substitutions = 0;
        int deletions = 0;
        int insertions = 0;
        QString resultConfiguration;
        QString resultCredentialHash;
    };

    [[nodiscard]] struct Entry *find(const QString &id);
    [[nodiscard]] const struct Entry *find(const QString &id) const;
    void load();
    bool persist();
    void notifyEntries();
    void setState(bool busy, QString status, QString error = {});
    void finishCurrent(quint64 generation, quint64 workerEpoch, const QString &id, const QString &transcript, const QString &workerError);
    void startNext(quint64 generation);
    void retireWorker();
    bool importWave(const QUrl &source, QString *path, double *duration, QString *error) const;
    static void score(const QString &expected, const QString &actual, int *substitutions,
        int *deletions, int *insertions, double *wer);

    QVector<struct Entry> m_entries;
    QString m_directory;
    QString m_provider = QStringLiteral("local");
    QString m_modelPath;
    QString m_model = QStringLiteral("whisper-large-v3-turbo");
    QString m_apiKey;
    bool m_busy = false;
    bool m_recording = false;
    QString m_status = QStringLiteral("Add a recording to begin.");
    QString m_error;
    int m_current = -1;
    quint64 m_generation = 0;
    quint64 m_workerGeneration = 0;
    quint64 m_nextWorkerGeneration = 0;
    QPointer<VoiceWorker> m_worker;
    QPointer<QThread> m_thread;
    QVector<QPointer<QThread>> m_threads;
    QAudioSource *m_audioSource = nullptr;
    QIODevice *m_audioDevice = nullptr;
    QByteArray m_recordedPcm;
    VoicePcmConverter m_converter;
};
