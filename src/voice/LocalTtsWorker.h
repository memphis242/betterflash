#pragma once

#include <QAudioFormat>
#include <QObject>
#include <atomic>
#include <memory>

class QTimer;

class LocalTtsWorker final : public QObject {
    Q_OBJECT
public:
    explicit LocalTtsWorker(QString runtimeDirectory, QString modelDirectory,
        QString modelFilename, QObject *parent = nullptr);
    ~LocalTtsWorker() override;
    void synthesize(const QString &text, const QAudioFormat &format, quint64 request,
        const std::shared_ptr<std::atomic_bool> &cancelled);
    void shutdown();
signals:
    void pcmReady(const QByteArray &pcm, quint64 request, double seconds);
    void failed(const QString &message, quint64 request);
private:
    struct Engine;
    bool load(QString *error);
    void release();
    const QString m_runtimeDirectory;
    const QString m_modelDirectory;
    const QString m_modelFilename;
    std::unique_ptr<struct Engine> m_engine{};
    QTimer *m_idle = nullptr;
    bool m_shuttingDown = false;
};
