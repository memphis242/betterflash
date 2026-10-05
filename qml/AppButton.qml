import QtQuick
import QtQuick.Controls

Button {
    id: control
    property bool primary: false
    property string hint: ""
    implicitHeight: 38
    implicitWidth: Math.max(38, label.implicitWidth + 26)
    font.family: Theme.uiFont
    font.pixelSize: 14
    padding: 10
    ToolTip.visible: hovered && hint.length > 0
    ToolTip.text: hint
    contentItem: Label {
        id: label
        textFormat: Text.PlainText
        text: control.text
        font: control.font
        color: control.enabled ? (control.primary ? Theme.onAccent : Theme.ink) : Theme.inkMuted
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: 4
        color: control.primary && control.enabled ? Theme.accent : (control.hovered || control.down ? Theme.accentSoft : Theme.transparent)
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus || control.primary && control.enabled ? Theme.accent : Theme.rule
        opacity: control.enabled ? 1 : 0.7
    }
}
