import QtQuick
import QtQuick.Controls

ToolButton {
    id: control
    property string glyph: "more"
    property string hint: ""
    property bool selected: false
    implicitWidth: 38
    implicitHeight: 38
    ToolTip.visible: hovered
    ToolTip.text: hint
    Accessible.name: hint
    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            kind: control.glyph
            stroke: control.selected ? Theme.accent : Theme.ink
        }
    }
    background: Rectangle {
        radius: width / 2
        color: control.hovered || control.down ? Theme.accentSoft : Theme.transparent
        border.width: control.activeFocus || control.selected ? 2 : 1
        border.color: control.activeFocus || control.selected ? Theme.accent : Theme.rule
    }
}
