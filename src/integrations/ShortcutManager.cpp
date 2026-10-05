#include "ShortcutManager.h"
#include <QKeySequence>

QVariantMap ShortcutManager::defaultBindings() {
    return {
        {QStringLiteral("commandPalette"), QStringLiteral("Ctrl+K")},
        {QStringLiteral("deckSearch"), QStringLiteral("Ctrl+L")},
        {QStringLiteral("newCard"), QStringLiteral("Ctrl+N")},
        {QStringLiteral("newDeck"), QStringLiteral("Ctrl+Shift+N")},
        {QStringLiteral("editCard"), QStringLiteral("Ctrl+E")},
        {QStringLiteral("review"), QStringLiteral("Space")},
        {QStringLiteral("gradeMissed"), QStringLiteral("1")},
        {QStringLiteral("gradePartial"), QStringLiteral("2")},
        {QStringLiteral("gradeHard"), QStringLiteral("3")},
        {QStringLiteral("gradeGood"), QStringLiteral("4")},
        {QStringLiteral("gradeEasy"), QStringLiteral("5")},
        {QStringLiteral("defer"), QStringLiteral("D")},
        {QStringLiteral("postpone"), QStringLiteral("S")},
        {QStringLiteral("pause"), QStringLiteral("P")},
        {QStringLiteral("reviewPage"), QStringLiteral("Alt+1")},
        {QStringLiteral("libraryPage"), QStringLiteral("Alt+2")},
        {QStringLiteral("historyPage"), QStringLiteral("Alt+3")},
        {QStringLiteral("settingsPage"), QStringLiteral("Alt+4")},
        {QStringLiteral("editorSplit"), QStringLiteral("Ctrl+Return")},
        {QStringLiteral("editorCloze"), QStringLiteral("Ctrl+Shift+C")},
        {QStringLiteral("editorImage"), QStringLiteral("Ctrl+Shift+I")},
        {QStringLiteral("saveCard"), QStringLiteral("Ctrl+S")}
    };
}
ShortcutManager::ShortcutManager(QObject *parent) : QObject(parent), m_bindings(defaultBindings()) {
    const QVariantMap stored = m_settings.value(QStringLiteral("keyboard/bindings")).toMap();
    for (auto it = stored.cbegin(); it != stored.cend(); ++it) {
        if (m_bindings.contains(it.key())) m_bindings[it.key()] = it.value();
    }
}
QString ShortcutManager::get(const QString &action) const { return m_bindings.value(action).toString(); }
QString ShortcutManager::sequenceForKey(const int key, const int modifiers) const {
    if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta) return {};
    return QKeySequence(QKeyCombination(Qt::KeyboardModifiers(modifiers), Qt::Key(key))).toString(QKeySequence::PortableText);
}
bool ShortcutManager::rebind(const QString &action, const QString &sequence) {
    m_lastError.clear();
    if (!defaultBindings().contains(action)) {
        m_lastError = QStringLiteral("SHORTCUT_ACTION: Choose an existing command.");
        emit changed(); return false;
    }
    const QKeySequence parsed = QKeySequence::fromString(sequence.trimmed(), QKeySequence::PortableText);
    const QString canonical = parsed.toString(QKeySequence::PortableText);
    if (parsed.isEmpty() || parsed.count() != 1 || canonical.isEmpty()
        || parsed[0].key() == Qt::Key_unknown || parsed[0].key() == Qt::Key_Escape) {
        m_lastError = QStringLiteral("SHORTCUT_INVALID: Choose one key combination. Escape is reserved for closing dialogs.");
        emit changed(); return false;
    }
    for (auto it = m_bindings.cbegin(); it != m_bindings.cend(); ++it) {
        if (it.key() != action && it.value().toString() == canonical) {
            m_lastError = QStringLiteral("SHORTCUT_CONFLICT: This combination is assigned to %1.").arg(it.key());
            emit changed(); return false;
        }
    }
    m_bindings[action] = canonical;
    m_settings.setValue(QStringLiteral("keyboard/bindings"), m_bindings);
    emit changed(); return true;
}
void ShortcutManager::reset() {
    m_bindings = defaultBindings();
    m_lastError.clear();
    m_settings.remove(QStringLiteral("keyboard/bindings"));
    emit changed();
}
