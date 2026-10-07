import QtQuick
import QtQuick.Controls

ToolButton {
    id: control
    required property int direction
    property string hint: ""
    padding: 5
    Accessible.name: hint
    ToolTip.visible: hovered || activeFocus
    ToolTip.text: hint
    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            width: 20
            height: 20
            kind: control.direction < 0 ? "arrow-left" : "arrow-right"
            stroke: control.activeFocus ? Theme.accent : Theme.inkMuted
            opacity: control.enabled ? 1 : 0.35
        }
    }
    background: Rectangle {
        radius: 9
        color: control.enabled && (control.hovered || control.down) ? Theme.accentSoft : Theme.transparent
        border.color: control.activeFocus ? Theme.accent : Theme.rule
        border.width: control.activeFocus ? 2 : 1
        opacity: control.enabled ? 1 : 0.35
    }
}
