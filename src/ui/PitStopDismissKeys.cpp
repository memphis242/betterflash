#include "ui/PitStopDismissKeys.h"

#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QKeyEvent>

namespace {
bool dismissalKey(const int key)
{
    switch (key) {
    case Qt::Key_Q:
    case Qt::Key_Space:
    case Qt::Key_Escape: return true;
    default: return false;
    }
}
bool withinScope(const QQuickItem *item, const QQuickItem *const scope)
{
    for (; item != nullptr; item = item->parentItem())
        if (item == scope) return true;
    return false;
}
}

PitStopDismissKeys::PitStopDismissKeys(QObject *parent) : QObject(parent)
{
    Q_ASSERT(QCoreApplication::instance() != nullptr);
    QCoreApplication::instance()->installEventFilter(this);
}
void PitStopDismissKeys::setScope(QQuickItem *const scope)
{
    if (m_scope == scope) return;
    m_scope = scope;
    emit scopeChanged();
}
void PitStopDismissKeys::setEnabled(const bool enabled)
{
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    emit enabledChanged();
}
bool PitStopDismissKeys::eventFilter(QObject *watched, QEvent *event)
{
    auto *const window = qobject_cast<QQuickWindow *>(watched);
    if (window == nullptr || event == nullptr) return false;
    if (event->type() == QEvent::WindowDeactivate && window == m_pressedWindow) {
        m_pressedKeys.clear();
        m_pressedWindow.clear();
        return false;
    }
    const QEvent::Type type = event->type();
    if (type != QEvent::ShortcutOverride && type != QEvent::KeyPress && type != QEvent::KeyRelease)
        return false;
    auto *const key = static_cast<QKeyEvent *>(event);
    if (window == m_pressedWindow && m_pressedKeys.contains(key->key())) {
        if (type == QEvent::KeyRelease && !key->isAutoRepeat()) {
            m_pressedKeys.remove(key->key());
            if (m_pressedKeys.isEmpty()) m_pressedWindow.clear();
        } else if (type == QEvent::KeyPress && !key->isAutoRepeat()) {
            m_pressedKeys.remove(key->key());
            if (m_pressedKeys.isEmpty()) m_pressedWindow.clear();
        } else {
            event->accept();
            return true;
        }
        if (type == QEvent::KeyRelease) { event->accept(); return true; }
    }
    if (type == QEvent::KeyRelease || !m_enabled || !m_scope || !m_scope->isVisible()
        || m_scope->window() != window || QGuiApplication::focusWindow() != window
        || key->modifiers() != Qt::NoModifier || !dismissalKey(key->key())
        || !withinScope(window->activeFocusItem(), m_scope)) return false;
    event->accept();
    if (type == QEvent::KeyPress) {
        m_pressedWindow = window;
        m_pressedKeys.insert(key->key());
        if (!key->isAutoRepeat()) emit dismissRequested();
    }
    return true;
}
