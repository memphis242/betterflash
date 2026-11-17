pragma ComponentBehavior: Bound
import QtQuick

AppMenu {
    id: menu
    required property var editor
    Component.onCompleted: console.assert(editor && typeof editor.selectAll === "function"
        && typeof editor.remove === "function", "TextEditingMenu requires an editable text control")
    readonly property bool editable: editor && editor.enabled && !editor.readOnly
    readonly property bool hasSelection: editor && editor.selectionStart !== editor.selectionEnd

    AppMenuItem {
        text: "Undo"
        glyph: "undo"
        hint: "Undo the last text edit (Ctrl+Z)."
        enabled: menu.editable && menu.editor.canUndo
        onTriggered: menu.editor.undo()
    }
    AppMenuItem {
        text: "Redo"
        glyph: "redo"
        hint: "Redo a text edit."
        enabled: menu.editable && menu.editor.canRedo
        onTriggered: menu.editor.redo()
    }
    AppMenuSeparator {}
    AppMenuItem {
        text: "Cut"
        glyph: "cut"
        hint: "Cut the selected text (Ctrl+X)."
        enabled: menu.editable && menu.hasSelection
        onTriggered: menu.editor.cut()
    }
    AppMenuItem {
        text: "Copy"
        glyph: "copy"
        hint: "Copy the selected text (Ctrl+C)."
        enabled: menu.hasSelection
        onTriggered: menu.editor.copy()
    }
    AppMenuItem {
        text: "Paste"
        glyph: "paste"
        hint: "Paste text at the caret, replacing any selection (Ctrl+V)."
        enabled: menu.editable && menu.editor.canPaste
        onTriggered: menu.editor.paste()
    }
    AppMenuItem {
        text: "Delete selection"
        glyph: "empty-box"
        hint: "Delete the selected text."
        enabled: menu.editable && menu.hasSelection
        onTriggered: menu.editor.remove(menu.editor.selectionStart, menu.editor.selectionEnd)
    }
    AppMenuSeparator {}
    AppMenuItem {
        text: "Select all"
        glyph: "select-all"
        hint: "Select all text (Ctrl+A)."
        enabled: menu.editor && menu.editor.length > 0
        onTriggered: menu.editor.selectAll()
    }
}
