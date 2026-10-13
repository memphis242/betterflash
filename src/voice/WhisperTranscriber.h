#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

class QProcess;
class QTemporaryFile;

class WhisperTranscriber final : public QObject {
    Q_OBJECT
public:
    explicit WhisperTranscriber(QString modelPath, QString vocabulary, QString acceleration, QObject *parent = nullptr);
    ~WhisperTranscriber() override;

    [[nodiscard]] bool busy() const { return !m_process.isNull(); }
    void submit(const QByteArray &pcm, quint64 epoch);
    void cancel();

signals:
    void transcriptReady(const QString &text, quint64 epoch);
    void errorOccurred(const QString &code, const QString &message, bool fatal, quint64 epoch);
    void finished(quint64 epoch);

private:
    void finish(quint64 generation, quint64 epoch);
    const QString m_modelPath;
    const QString m_vocabulary;
    const QString m_acceleration;
    QPointer<QProcess> m_process;
    QPointer<QTemporaryFile> m_audioFile;
    QByteArray m_stdout;
    QByteArray m_stderr;
    bool m_reportedOverflow = false;
    quint64 m_generation = 0;
};
