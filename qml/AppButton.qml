import QtQuick
import QtQuick.Controls

Button {
    id: control
    property bool primary: false
    property string hint: ""
    property string glyph: ""
    property real glyphSize: 17
    property color foreground: primary ? Theme.onAccent : Theme.ink
    property color fill: primary ? Theme.accent : Theme.transparent
    property color hoverFill: Theme.accentSoft
    property color outline: primary ? Theme.accent : Theme.rule
    property color focusOutline: Theme.accent
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
                stroke: control.enabled ? control.foreground : Theme.inkMuted
                anchors.verticalCenter: parent.verticalCenter
            }
            Label {
                id: label
                width: Math.max(0, contentRow.width - control.glyphSpan)
                height: parent.height
                textFormat: Text.PlainText
                text: control.text
                font: control.font
                color: control.enabled ? control.foreground : Theme.inkMuted
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }
    }
    background: Rectangle {
        radius: 4
        color: !control.enabled ? Theme.transparent : control.primary ? control.fill : control.hovered || control.down ? control.hoverFill : control.fill
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? control.focusOutline : control.enabled ? control.outline : Theme.rule
        opacity: control.enabled ? 1 : 0.7
    }
}
