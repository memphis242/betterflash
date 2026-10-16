#pragma once

#include <QObject>
#include <QVariantList>

#include <memory>

class ApiKeyBinding;
class SecretStore;

// Owns named credential entries while keeping their values in SecretStore.
class ApiKeyRegistry final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList keyChoices READ keyChoices NOTIFY changed)
public:
    explicit ApiKeyRegistry(QObject *parent = nullptr);
    ~ApiKeyRegistry() override;

    QVariantList keyChoices() const;
    ApiKeyBinding *binding(const QString &source, QObject *parent = nullptr);

    static QString canonicalOrigin(const QString &endpoint);
    static bool validSource(const QString &source);
    bool canSelectKey(const QString &id, const QString &endpoint) const;
    QString error() const;

signals:
    void changed();

private:
    friend class ApiKeyBinding;

    struct Entry;
    Entry *ensureEntry(const QString &source, const QString &origin, bool legacy = false);
    QString ensureOwnEntryId(const QString &source, const QString &origin, QString *error = nullptr);
    Entry *entry(const QString &id) const;
    bool entryMatches(const QString &id, const QString &origin) const;
    QString entrySource(const QString &id) const;
    SecretStore *entryStore(const QString &id) const;
    QVariantList choicesFor(const QString &source, const QString &origin, const QString &selectedId) const;
    bool selectFor(const QString &source, const QString &origin, const QString &id, QString *error = nullptr);
    QString selectionFor(const QString &source, const QString &origin) const;
    bool persist(QString *error = nullptr);
    void migrateLegacy();
    void connectEntry(Entry *entry);

    struct Private;
    std::unique_ptr<Private> d;
};
