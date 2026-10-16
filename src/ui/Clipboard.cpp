#include "Clipboard.h"

#include <QClipboard>
#include <QGuiApplication>

Clipboard::Clipboard(QObject *parent)
    : QObject(parent) {}

bool Clipboard::copyText(const QString &text) const {
    QClipboard *const clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr)
        return false;
    clipboard->setText(text, QClipboard::Clipboard);
    return true;
}
