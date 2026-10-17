#include "actionhistory.h"
#include "appcontroller.h"

#include <QMetaMethod>
#include <QMetaProperty>
#include <QThread>
#include <algorithm>
#include <cassert>
#include <utility>

ActionHistory::ActionHistory(AppController *app, QObject *parent) : QObject(parent), m_app(app)
{
    assert(m_app);
    m_flush.setSingleShot(true);
    m_flush.setInterval(0);
    connect(&m_flush, &QTimer::timeout, this, &ActionHistory::flush);
    connect(m_app, &AppController::busyChanged, this, &ActionHistory::changed);
    connect(m_app, &AppController::historyInvalidated, this, &ActionHistory::invalidate);
    connect(m_app, &AppController::actionRecorded, this,
        [this](const QString &id, const QString &label, const QVariantMap &before, const QVariantMap &after) {
            flush();
            push({id, label, before, after, {}, {}});
        });
    connect(m_app, &AppController::historyApplied, this,
        [this](const QString &id, bool redo, bool success) {
            if (id != m_pendingId || redo != m_pendingRedo) return;
            assert(m_replaying);
            if (success) {
                const qsizetype index = redo ? m_cursor : m_cursor - 1;
                assert(index >= 0 && index < m_entries.size());
                const struct Entry &entry = m_entries.at(index);
                m_app->restoreHistoryContext(redo ? entry.afterContext : entry.beforeContext);
                m_cursor += redo ? 1 : -1;
            } else m_app->restoreHistoryContext(m_replayOrigin);
            m_pendingId.clear();
            m_replaying = false;
            if (m_invalidationPending) invalidate();
            observe();
            emit changed();
        });
}
bool ActionHistory::busy() const { return m_replaying || m_app->busy(); }
bool ActionHistory::canUndo() const { return m_cursor > 0 && !busy(); }
bool ActionHistory::canRedo() const { return m_cursor < m_entries.size() && !busy(); }
QString ActionHistory::undoLabel() const { return m_cursor > 0 ? m_entries.at(m_cursor - 1).label : QString{}; }
QString ActionHistory::redoLabel() const { return m_cursor < m_entries.size() ? m_entries.at(m_cursor).label : QString{}; }
void ActionHistory::push(struct Entry entry)
{
    assert(!m_replaying);
    QStringList discarded;
    while (m_entries.size() > m_cursor) {
        const struct Entry removed = m_entries.takeLast();
        if (!removed.id.isEmpty()) discarded.append(removed.id);
    }
    m_entries.append(std::move(entry));
    if (m_entries.size() > Capacity) {
        const struct Entry removed = m_entries.takeFirst();
        if (!removed.id.isEmpty()) discarded.append(removed.id);
    }
    m_cursor = m_entries.size();
    if (!discarded.isEmpty()) m_app->forgetHistory(discarded);
    emit changed();
}
void ActionHistory::recordLocal(QString label, std::function<bool()> undo, std::function<bool()> redo)
{
    if (m_replaying) return;
    flush();
    push({{}, std::move(label), {}, {}, std::move(undo), std::move(redo)});
}
void ActionHistory::undo() { apply(false); }
void ActionHistory::redo() { apply(true); }
void ActionHistory::apply(bool redo)
{
    flush();
    if (redo ? !canRedo() : !canUndo()) return;
    const struct Entry &entry = m_entries.at(redo ? m_cursor : m_cursor - 1);
    m_replaying = true;
    emit changed();
    if (!entry.id.isEmpty()) {
        m_replayOrigin = m_app->historyContext();
        m_pendingId = entry.id;
        m_pendingRedo = redo;
        if (m_app->applyHistory(entry.id, redo)) return;
        m_pendingId.clear();
    } else {
        const bool success = (redo ? entry.redo : entry.undo)();
        if (success) m_cursor += redo ? 1 : -1;
        else fail(QStringLiteral("This action could not be reversed. Its data or settings have changed. The history position was preserved."));
    }
    m_replaying = false;
    if (m_invalidationPending) invalidate();
    observe();
    emit changed();
}
void ActionHistory::fail(const QString &message)
{
    m_app->reportHistoryError(QStringLiteral("UNDO_CONFLICT"), message);
}
void ActionHistory::invalidate()
{
    if (m_replaying) { m_invalidationPending = true; return; }
    m_invalidationPending = false;
    m_flush.stop();m_pending.clear();
    QStringList ids;
    for (const struct Entry &entry : std::as_const(m_entries)) if (!entry.id.isEmpty()) ids.append(entry.id);
    m_entries.clear();m_cursor = 0;
    if (!ids.isEmpty()) m_app->forgetHistory(ids);
    emit changed();
}
void ActionHistory::watchProperties(QObject *object, const QString &label, const QStringList &properties)
{
    if (!object || properties.isEmpty()) return;
    QList<QByteArray> names;
    QList<QMetaMethod> notifications;
    for (const QString &name : properties) {
        const QByteArray bytes = name.toUtf8();
        const int index = object->metaObject()->indexOfProperty(bytes.constData());
        if (index < 0) continue;
        const QMetaProperty property = object->metaObject()->property(index);
        if (!property.isReadable() || !property.isWritable() || !property.hasNotifySignal()) continue;
        names.append(bytes);
        if (!notifications.contains(property.notifySignal())) notifications.append(property.notifySignal());
    }
    if (names.isEmpty()) return;
    const QPointer<QObject> guarded(object);
    watchState(object, label, [guarded, names]() -> QVariant {
        QVariantMap values;
        if (guarded) for (const QByteArray &name : names) values.insert(QString::fromUtf8(name), guarded->property(name.constData()));
        return values;
    }, [guarded, names](const QVariant &state) {
        if (!guarded) return false;
        const QVariantMap values = state.toMap();
        for (const QByteArray &name : names) {
            const QVariant value = values.value(QString::fromUtf8(name));
            if (!value.isValid()) return false;
            if (guarded->property(name.constData()) != value && !guarded->setProperty(name.constData(), value)) return false;
        }
        for (const QByteArray &name : names)
            if (guarded->property(name.constData()) != values.value(QString::fromUtf8(name))) return false;
        return true;
    });
    const QMetaMethod slot = metaObject()->method(metaObject()->indexOfSlot("observe()"));
    for (const QMetaMethod &signal : notifications) QObject::connect(object, signal, this, slot);
}
void ActionHistory::watchState(QObject *object, QString label, std::function<QVariant()> read,
                               std::function<bool(const QVariant &)> write)
{
    assert(object && read && write);
    const QVariant current = read();
    m_watches.append({object, std::move(label), std::move(read), std::move(write), current});
}
void ActionHistory::observe()
{
    assert(thread() == QThread::currentThread());
    for (qsizetype i = 0; i < m_watches.size(); ++i) {
        struct Watch &watch = m_watches[i];
        if (!watch.object) continue;
        const QVariant value = watch.read();
        if (value == watch.current) continue;
        if (!m_replaying) {
            auto found = std::find_if(m_pending.begin(), m_pending.end(), [i](const struct SettingChange &change) { return change.watch == i; });
            if (found == m_pending.end()) m_pending.append({i, watch.current, value});
            else found->after = value;
            m_flush.start();
        }
        watch.current = value;
    }
}
void ActionHistory::flush()
{
    m_flush.stop();
    if (m_pending.isEmpty()) return;
    QList<struct SettingChange> changes = std::exchange(m_pending, {});
    changes.removeIf([](const struct SettingChange &change) { return change.before == change.after; });
    if (changes.isEmpty() || m_replaying) return;
    const QString label = m_watches.at(changes.first().watch).label;
    push({{}, label, {}, {}, [this, changes] { return applySettings(changes, false); },
          [this, changes] { return applySettings(changes, true); }});
}
bool ActionHistory::applySettings(const QList<struct SettingChange> &changes, bool redo)
{
    for (const struct SettingChange &change : changes) {
        const struct Watch &watch = m_watches.at(change.watch);
        if (!watch.object || watch.read() != (redo ? change.before : change.after)) return false;
    }
    QList<qsizetype> attempted;
    for (qsizetype i = 0; i < changes.size(); ++i) {
        const struct SettingChange &change = changes.at(i);
        struct Watch &watch = m_watches[change.watch];
        attempted.append(i);
        if (!watch.write(redo ? change.after : change.before)
            || watch.read() != (redo ? change.after : change.before)) {
            for (auto it = attempted.crbegin(); it != attempted.crend(); ++it) {
                const struct SettingChange &previous = changes.at(*it);
                struct Watch &restore = m_watches[previous.watch];
                if (!restore.write(redo ? previous.before : previous.after))
                    fail(QStringLiteral("Settings could not be restored completely. Review their current values before continuing."));
            }
            return false;
        }
    }
    return true;
}
