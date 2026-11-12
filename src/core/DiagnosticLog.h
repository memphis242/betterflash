#pragma once

#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

class DiagnosticLog final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString level READ level WRITE setLevel NOTIFY changed)
    Q_PROPERTY(QString verbosity READ verbosity WRITE setVerbosity NOTIFY changed)
    Q_PROPERTY(QString logDirectory READ logDirectory CONSTANT)
    Q_PROPERTY(QString lastLogPath READ lastLogPath NOTIFY changed)
    Q_PROPERTY(QVariantMap errorInfo READ errorInfo NOTIFY changed)
public:
    explicit DiagnosticLog(const QString &dataDirectory, QObject *parent = nullptr);
    ~DiagnosticLog() override;

    static DiagnosticLog *instance();
    [[nodiscard]] QString level() const;
    [[nodiscard]] QString verbosity() const;
    [[nodiscard]] QString logDirectory() const { return m_directory; }
    [[nodiscard]] QString lastLogPath() const;
    [[nodiscard]] QVariantMap errorInfo() const;
    void setLevel(const QString &level);
    void setVerbosity(const QString &verbosity);
    void addSecret(const QString &secret);
    [[nodiscard]] QString redact(const QString &value) const;
    [[nodiscard]] QString record(const QString &level, const QString &event, const QJsonObject &details = {});
    Q_INVOKABLE void openLogDirectory();
    Q_INVOKABLE void acknowledgeError();

signals:
    void changed();

private:
    [[nodiscard]] QString redactUnlocked(QString value) const;
    void setErrorUnlocked(const QString &code, const QString &message, const QString &detail);
    void notifyChanged();
    const QString m_directory;
    mutable QMutex m_mutex{};
    QString m_level{QStringLiteral("warning")};
    QString m_verbosity{QStringLiteral("high")};
    QString m_lastLogPath{};
    QVariantMap m_errorInfo{};
    QList<QVariantMap> m_pendingErrors{};
    QStringList m_secrets{};
    QtMessageHandler m_previousHandler = nullptr;
};
