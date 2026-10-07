import QtQuick
import QtQuick.Controls

Rectangle {
    id: badge
    required property string label
    property real maximumWidth: 200
    width: Math.min(maximumWidth, caption.implicitWidth + 16)
    height: 24
    radius: height / 2
    color: Theme.transparent
    border.color: Theme.rule
    Accessible.name: "Tag: " + label
    ToolTip.visible: hover.hovered
    ToolTip.text: "Tag: " + label
    HoverHandler {
        id: hover
    }
    Label {
        id: caption
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        verticalAlignment: Text.AlignVCenter
        textFormat: Text.PlainText
        text: badge.label
        color: Theme.inkMuted
        font.family: Theme.monoFont
        font.pixelSize: 11
        elide: Text.ElideRight
    }
}
