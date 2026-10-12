#pragma once

#include <QCache>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QQueue>
#include <QSet>
#include <QTemporaryDir>
#include <QTimer>

struct SemanticToken {
    int line = 0;
    int column = 0;
    int length = 0;
    QString kind;
};

// One bounded, asynchronous language-server session. No snippet is executed.
class LspHighlighter final : public QObject {
    Q_OBJECT
public:
    explicit LspHighlighter(QObject *parent = nullptr);
    ~LspHighlighter() override;
    void configure(bool enabled, const QString &program);
    QList<struct SemanticToken> tokens(const QString &language, const QString &source, bool requestIfMissing, bool &accepted);
    void retain(const QSet<QString> &keys);
    static QString key(const QString &language, const QString &source);
    QString status() const { return m_status; }
signals:
    void ready(const QString &key);
    void statusChanged();
    void capacityAvailable();
private:
    struct Job { QString key; QString source; QString language; };
    void start();
    void send(const QString &method, const QJsonObject &parameters, int id = 0);
    void read();
    void response(const QJsonObject &object);
    void next();
    void fail(const QString &message);
    void stop();
    void setStatus(const QString &status);
    QProcess m_process;
    QTemporaryDir m_directory;
    QTimer m_timeout;
    QTimer m_idle;
    QCache<QString, QList<struct SemanticToken>> m_cache{2 * 1024 * 1024};
    QQueue<struct Job> m_queue;
    QSet<QString> m_pending;
    QSet<QString> m_liveKeys;
    struct Job m_current;
    QStringList m_legend;
    QByteArray m_buffer;
    QString m_program;
    QString m_status;
    QString m_uri;
    int m_nextId = 2;
    int m_requestId = 0;
    bool m_enabled = true;
    bool m_initialized = false;
    bool m_failed = false;
    bool m_stopping = false;
};
