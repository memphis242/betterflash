import QtQuick
import QtQuick.Controls

TabButton {
    id: control
    font.family: Theme.uiFont
    font.bold: checked

    contentItem: Label {
        text: control.text
        textFormat: Text.PlainText
        font: control.font
        color: control.checked ? Theme.editorTabActiveInk : Theme.inkMuted
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        opacity: control.enabled ? 1 : 0.55
    }
    background: Rectangle {
        radius: 7
        color: control.checked ? Theme.editorTabActive : Theme.editorTabInactive
        border.width: control.checked || control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? Theme.accent
            : control.checked ? Theme.editorTabActiveInk
            : control.enabled && control.hovered ? Theme.inkMuted : Theme.rule
        opacity: control.enabled ? 1 : 0.55
    }
}
