#pragma once

#include <QObject>
#include <QSettings>
#include <QVariantMap>

class ShortcutManager final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap bindings READ bindings NOTIFY changed)
    Q_PROPERTY(QVariantMap defaults READ defaults CONSTANT)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
    Q_PROPERTY(bool customized READ customized NOTIFY changed)
public:
    explicit ShortcutManager(QObject *parent = nullptr);
    QVariantMap bindings() const { return m_bindings; }
    QVariantMap defaults() const { return defaultBindings(); }
    QString lastError() const { return m_lastError; }
    bool customized() const { return m_bindings != defaultBindings(); }
    Q_INVOKABLE QString get(const QString &action) const;
    Q_INVOKABLE bool rebind(const QString &action, const QString &sequence);
    Q_INVOKABLE QString sequenceForKey(int key, int modifiers) const;
    Q_INVOKABLE void reset();
signals:
    void changed();
private:
    static QVariantMap defaultBindings();
    QVariantMap m_bindings;
    QString m_lastError;
    QSettings m_settings;
};

