#pragma once

#include <QObject>
#include <QVariantList>

class ApiKeyRegistry;

// A consumer-facing selection of one registry entry. It never exposes the raw secret to QML.
class ApiKeyBinding final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString ownKeyName READ ownKeyName NOTIFY changed)
    Q_PROPERTY(QString ownKeyId READ ownKeyId NOTIFY changed)
    Q_PROPERTY(QString selectedKeyId READ selectedKeyId NOTIFY changed)
    Q_PROPERTY(QString keyName READ keyName NOTIFY changed)
    Q_PROPERTY(QString sourcePage READ sourcePage NOTIFY changed)
    Q_PROPERTY(bool reusingKey READ reusingKey NOTIFY changed)
    Q_PROPERTY(QVariantList keyChoices READ keyChoices NOTIFY changed)
    Q_PROPERTY(bool hasKey READ hasKey NOTIFY changed)
    Q_PROPERTY(QString storageState READ storageState NOTIFY changed)
    Q_PROPERTY(QString storageStatus READ storageStatus NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    ApiKeyBinding(ApiKeyRegistry *registry, QString source, QObject *parent = nullptr);
    ~ApiKeyBinding() override;

    QString ownKeyName() const;
    QString ownKeyId() const;
    QString selectedKeyId() const;
    QString keyName() const;
    QString sourcePage() const;
    bool reusingKey() const;
    QVariantList keyChoices() const;
    bool hasKey() const;
    QString storageState() const;
    QString storageStatus() const;
    QString error() const;

    // Native callers resolve the selected secret without making it a Qt property or invokable.
    QString key() const;
    ApiKeyRegistry *registry() const { return m_registry; }
    void setEndpoint(const QString &endpoint);

    Q_INVOKABLE bool selectKey(const QString &id);
    Q_INVOKABLE bool saveKey(const QString &key);
    Q_INVOKABLE bool clearKey();

signals:
    void changed();

private:
    void refresh();
    ApiKeyRegistry *const m_registry;
    const QString m_source;
    QString m_origin;
    QString m_ownId;
    QString m_selectedId;
    QString m_error;
};
