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
        Menu {
            id: selectionMenu
            MenuItem { text: "Copy"; enabled: view.hasSelection; onTriggered: view.copySelection() }
            MenuItem { text: "Select all"; onTriggered: view.selectAll() }
        }
    }
    Label {
        textFormat: Text.PlainText
        id: failure
        visible: text.length > 0
        text: view.renderError
        color: Theme.error
        font.family: Theme.uiFont
        font.pixelSize: 12
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }
}
