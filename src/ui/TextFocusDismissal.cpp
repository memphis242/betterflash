#include "ui/TextFocusDismissal.h"

#include <QEvent>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTouchEvent>

namespace {

bool acceptsTextInput(const QQuickItem *const item)
{
    return item != nullptr && item->flags().testFlag(QQuickItem::ItemAcceptsInputMethod);
}

QQuickItem *editorViewport(QQuickItem *const editor)
{
    if (editor == nullptr)
        return nullptr;

    const QVariant viewportValue = editor->property("editorViewport");
    if (viewportValue.isValid() && viewportValue.canConvert<QQuickItem *>()) {
        if (QQuickItem *const viewport = viewportValue.value<QQuickItem *>())
            return viewport;
    }
    if (viewportValue.isValid() && viewportValue.canConvert<QObject *>()) {
        if (QQuickItem *const viewport = qobject_cast<QQuickItem *>(viewportValue.value<QObject *>()))
            return viewport;
    }
    return editor;
}

QQuickItem *focusedEditor(QQuickItem *item)
{
    while (item != nullptr) {
        if (acceptsTextInput(item))
            return item;
        item = item->parentItem();
    }
    return nullptr;
}

bool containsScenePoint(const QQuickItem *const item, const QPointF &scenePosition)
{
    if (item == nullptr || !item->isVisible())
        return false;

    const QQuickItem *current = item;
    while (current != nullptr) {
        if (current->clip()) {
            const QPointF localPosition = current->mapFromScene(scenePosition);
            if (!current->contains(localPosition))
                return false;
        }
        current = current->parentItem();
    }

    const QPointF localPosition = item->mapFromScene(scenePosition);
    return item->contains(localPosition);
}

QPointF scenePositionForEvent(const QEvent *const event, bool *const valid)
{
    Q_ASSERT(valid != nullptr);
    *valid = true;

    if (const QMouseEvent *const mouseEvent = dynamic_cast<const QMouseEvent *>(event))
        return mouseEvent->position();

    if (const QTouchEvent *const touchEvent = dynamic_cast<const QTouchEvent *>(event)) {
        const QList<QEventPoint> points = touchEvent->points();
        if (!points.isEmpty())
            return points.constFirst().position();
    }

    *valid = false;
    return {};
}

}

TextFocusDismissal::TextFocusDismissal(QObject *parent)
    : QObject(parent)
{
    QGuiApplication *const application = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
    Q_ASSERT(application != nullptr);
    application->installEventFilter(this);
}

bool TextFocusDismissal::eventFilter(QObject *watched, QEvent *event)
{
    if (event == nullptr)
        return QObject::eventFilter(watched, event);

    const bool isMousePress = event->type() == QEvent::MouseButtonPress;
    const bool isTouchBegin = event->type() == QEvent::TouchBegin;
    if (!isMousePress && !isTouchBegin)
        return QObject::eventFilter(watched, event);

    QQuickWindow *const window = qobject_cast<QQuickWindow *>(watched);
    if (window == nullptr)
        return QObject::eventFilter(watched, event);

    if (isMousePress) {
        const QMouseEvent *const mouseEvent = dynamic_cast<const QMouseEvent *>(event);
        if (mouseEvent == nullptr || mouseEvent->button() != Qt::LeftButton)
            return QObject::eventFilter(watched, event);
    }

    QQuickItem *const editor = focusedEditor(window->activeFocusItem());
    if (editor == nullptr)
        return QObject::eventFilter(watched, event);

    bool validPosition = false;
    const QPointF scenePosition = scenePositionForEvent(event, &validPosition);
    if (!validPosition)
        return QObject::eventFilter(watched, event);

    QQuickItem *const viewport = editorViewport(editor);
    if (viewport == nullptr || viewport->window() != window)
        return QObject::eventFilter(watched, event);

    if (!containsScenePoint(viewport, scenePosition))
        editor->setFocus(false, Qt::MouseFocusReason);

    return QObject::eventFilter(watched, event);
}
