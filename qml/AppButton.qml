import QtQuick
import QtQuick.Controls

Button {
    id: control
    property bool primary: false
    property string hint: ""
    property string glyph: ""
    property real glyphSize: 17
    readonly property real glyphSpan: control.glyph.length > 0 ? control.glyphSize + 8 : 0
    implicitHeight: 38
    implicitWidth: Math.max(38, label.implicitWidth + control.glyphSpan + 26)
    font.family: Theme.uiFont
    font.pixelSize: 14
    padding: 10
    ToolTip.visible: hovered && hint.length > 0
    ToolTip.text: hint
    contentItem: Item {
        Row {
            id: contentRow
            anchors.centerIn: parent
            width: Math.min(parent.width, label.implicitWidth + control.glyphSpan)
            height: parent.height
            spacing: control.glyph.length > 0 ? 8 : 0
            Icon {
                visible: control.glyph.length > 0
                width: control.glyphSize
                height: control.glyphSize
                kind: control.glyph
                stroke: control.enabled ? (control.primary ? Theme.onAccent : Theme.ink) : Theme.inkMuted
                anchors.verticalCenter: parent.verticalCenter
            }
            Label {
                id: label
                width: Math.max(0, contentRow.width - control.glyphSpan)
                height: parent.height
                textFormat: Text.PlainText
                text: control.text
                font: control.font
                color: control.enabled ? (control.primary ? Theme.onAccent : Theme.ink) : Theme.inkMuted
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }
    }
    background: Rectangle {
        radius: 4
        color: control.primary && control.enabled ? Theme.accent : (control.hovered || control.down ? Theme.accentSoft : Theme.transparent)
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus || control.primary && control.enabled ? Theme.accent : Theme.rule
        opacity: control.enabled ? 1 : 0.7
    }
}
