import QtQuick
import QtQuick.Controls

MenuItem {
    id: item
    property string glyph: ""
    property bool danger: false
    property string hint: ""
    readonly property color actionInk: !enabled ? Theme.inkMuted : danger ? Theme.error : Theme.ink
    readonly property real checkSpace: checkable && indicator ? indicator.width + spacing : 0
    readonly property real submenuSpace: subMenu && arrow ? arrow.width + spacing : 0
    readonly property bool hasGlyph: glyph.length > 0 || icon.source.toString().length > 0
    implicitHeight: 48
    implicitWidth: Math.max(240, Math.ceil(labelMetrics.advanceWidth) + leftPadding + rightPadding
        + checkSpace + submenuSpace + (hasGlyph ? 30 : 0))
    leftPadding: 14
    rightPadding: 14
    topPadding: 8
    bottomPadding: 8
    spacing: 12
    hoverEnabled: true
    font.family: Theme.uiFont
    font.pixelSize: 15
    palette.windowText: enabled ? (danger ? Theme.error : Theme.ink) : Theme.inkMuted
    ToolTip.visible: hovered && hint.length > 0
    ToolTip.text: hint
    TextMetrics { id: labelMetrics; text: item.text; font: item.font }
    contentItem: Item {
        implicitHeight: labelMetrics.height
        opacity: item.enabled ? 1 : 0.6
        Row {
            x: item.mirrored ? item.submenuSpace : item.checkSpace
            width: Math.max(0, parent.width - item.checkSpace - item.submenuSpace)
            height: parent.height
            spacing: 12
            layoutDirection: item.mirrored ? Qt.RightToLeft : Qt.LeftToRight
            Item {
                visible: item.hasGlyph
                width: 18
                height: 18
                anchors.verticalCenter: parent.verticalCenter
                Icon {
                    anchors.fill: parent
                    visible: item.glyph.length > 0
                    kind: item.glyph
                    stroke: item.enabled && item.danger ? Theme.error : Theme.inkMuted
                }
                Image {
                    anchors.fill: parent
                    visible: item.glyph.length === 0
                    source: item.icon.source
                    fillMode: Image.PreserveAspectFit
                }
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: item.text
                textFormat: Text.PlainText
                color: item.actionInk
                font: item.font
                elide: Text.ElideRight
                width: Math.max(0, parent.width - (item.hasGlyph ? 30 : 0))
            }
        }
    }
    background: Rectangle {
        radius: 8
        color: item.enabled && (item.hovered || item.highlighted) ? Theme.accentSoft : Theme.transparent
        border.width: item.activeFocus ? 1 : 0
        border.color: Theme.accent
    }
}
