import QtQuick
import QtQuick.Controls

ToolButton {
    id: control
    property bool collapsed: false
    property bool rightSide: false
    property string expandGlyph: "menu-bars"
    property string collapseGlyph: rightSide ? "dock-right" : "dock-left"
    property string hint: collapsed ? "Expand pane" : "Collapse pane"
    implicitWidth: 52
    implicitHeight: 48
    padding: 0
    ToolTip.visible: hovered || activeFocus
    ToolTip.text: hint
    Accessible.name: hint
    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            width: 26
            height: 26
            kind: control.collapsed ? control.expandGlyph : control.collapseGlyph
            stroke: control.hovered || control.activeFocus ? Theme.ink : Theme.inkMuted
        }
    }
    background: Rectangle {
        radius: 7
        color: control.hovered || control.down ? Theme.accentSoft : Theme.transparent
        border.width: control.activeFocus ? 2 : 0
        border.color: Theme.accent
    }
}
