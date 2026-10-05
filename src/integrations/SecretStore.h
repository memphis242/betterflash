#pragma once

#include <QObject>
#include <QQueue>
#include <QStringList>
#include <functional>

// The secret is readable only from native code. QML sees availability and status.
class SecretStore final : public QObject {
    Q_OBJECT
public:
    explicit SecretStore(QString service, QString environmentVariable = {}, QObject *parent = nullptr);
    ~SecretStore() override;
    QString key() const { return m_key; }
    bool hasKey() const { return !m_key.isEmpty(); }
    QString status() const { return m_status; }
    void setKey(const QString &key);
    void clear();
signals:
    void changed();
private:
    void load();
    void run(const QStringList &arguments, const QByteArray &input,
             const std::function<void(int, const QByteArray &)> &callback);
    void runNext();
    struct Operation {
        QStringList arguments;
        QByteArray input;
        std::function<void(int, const QByteArray &)> callback;
    };
    const QString m_service;
    QQueue<struct Operation> m_operations;
    QString m_key;
    QString m_status;
    quint64 m_revision = 0;
    bool m_running = false;
};
