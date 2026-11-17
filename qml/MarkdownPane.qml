import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BetterFlash.Native 1.0 as Native

ColumnLayout {
    id: pane
    property alias markdown: view.markdown
    property alias baseFontSize: view.baseFontSize
    property alias mediaRoot: view.mediaRoot
    property alias renderError: view.renderError
    property alias clozePreview: view.clozePreview
    property alias selectable: view.selectable
    spacing: 8
    implicitHeight: view.implicitHeight + (failure.visible ? failure.implicitHeight + spacing : 0)
    Native.MarkdownView {
        id: view
        Layout.fillWidth: true
        Layout.preferredHeight: view.implicitHeight
        darkMode: Theme.dark
        foreground: Theme.ink
        codeBackground: Theme.canvas
        accent: Theme.accent
        selectionBackground: Theme.accent
        selectionForeground: Theme.onAccent
        baseFontSize: 20
        onContextMenuRequested: (x, y) => selectionMenu.popup(x, y)
        AppMenu {
            id: selectionMenu
            AppMenuItem { text: "Copy"; glyph: "copy"; enabled: view.hasSelection; onTriggered: view.copySelection() }
            AppMenuItem { text: "Select all"; glyph: "select-all"; onTriggered: view.selectAll() }
        }
    }
    ErrorBlock {
        id: failure
        message: view.renderError
        textSize: 12
        Layout.fillWidth: true
    }
}
