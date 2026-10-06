import QtQuick
import QtQuick.Controls

ToolButton {
    id: control
    property string glyph: "more"
    property string hint: ""
    property bool selected: false
    property real glyphSize: 18
    property bool accentOnHover: false
    property bool toolTipOnFocus: false
    implicitWidth: 38
    implicitHeight: 38
    ToolTip.visible: hovered || toolTipOnFocus && activeFocus
    ToolTip.text: hint
    Accessible.name: hint
    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            width: control.glyphSize
            height: control.glyphSize
            kind: control.glyph
            stroke: control.selected || control.accentOnHover && (control.hovered || control.down || control.activeFocus) ? Theme.accent : Theme.ink
        }
    }
    background: Rectangle {
        radius: width / 2
        color: control.hovered || control.down ? Theme.accentSoft : Theme.transparent
        border.width: control.activeFocus || control.selected ? 2 : 1
        border.color: control.activeFocus || control.selected || control.accentOnHover && (control.hovered || control.down) ? Theme.accent : Theme.rule
    }
}
