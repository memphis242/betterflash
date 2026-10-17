#pragma once

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <functional>

class AppController;

class ActionHistory final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(QString undoLabel READ undoLabel NOTIFY changed)
    Q_PROPERTY(QString redoLabel READ redoLabel NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
public:
    explicit ActionHistory(AppController *app, QObject *parent = nullptr);
    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool canRedo() const;
    [[nodiscard]] QString undoLabel() const;
    [[nodiscard]] QString redoLabel() const;
    [[nodiscard]] bool busy() const;
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void watchProperties(QObject *object, const QString &label, const QStringList &properties);
    void watchState(QObject *object, QString label, std::function<QVariant()> read,
                    std::function<bool(const QVariant &)> write);
    void recordLocal(QString label, std::function<bool()> undo, std::function<bool()> redo);
    void flush();
    [[nodiscard]] bool replaying() const { return m_replaying; }
public slots:
    void observe();
signals:
    void changed();
private:
    static constexpr qsizetype Capacity = 5;
    static_assert(Capacity > 0);
    struct Entry {
        QString id;
        QString label;
        QVariantMap beforeContext;
        QVariantMap afterContext;
        std::function<bool()> undo;
        std::function<bool()> redo;
    };
    struct Watch {
        QPointer<QObject> object;
        QString label;
        std::function<QVariant()> read;
        std::function<bool(const QVariant &)> write;
        QVariant current;
    };
    struct SettingChange { qsizetype watch = 0; QVariant before; QVariant after; };
    void push(struct Entry entry);
    void apply(bool redo);
    bool applySettings(const QList<struct SettingChange> &changes, bool redo);
    void fail(const QString &message);
    void invalidate();
    AppController *const m_app;
    QList<struct Entry> m_entries;
    QList<struct Watch> m_watches;
    QList<struct SettingChange> m_pending;
    QTimer m_flush;
    qsizetype m_cursor = 0;
    bool m_replaying = false;
    QString m_pendingId;
    bool m_pendingRedo = false;
    QVariantMap m_replayOrigin;
    bool m_invalidationPending = false;
};
