import QtQuick
import QtQuick.Controls

Item {
    id: control
    property bool shown: false
    property string reason: "This narration may no longer match the card text."
    property string hint: shown ? reason + " Confirm it in the narration editor." : ""
    implicitWidth: shown ? 24 : 0
    implicitHeight: shown ? 24 : 0
    visible: shown
    activeFocusOnTab: shown
    Accessible.role: Accessible.StaticText
    Accessible.name: hint

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: Theme.transparent
        border.color: Theme.warning
        border.width: 1
    }
    Label {
        anchors.centerIn: parent
        text: "!"
        color: Theme.warning
        font.family: Theme.monoFont
        font.pixelSize: 14
        font.bold: true
    }
    ToolTip.visible: control.visible && (hoverHandler.hovered || control.activeFocus)
    ToolTip.text: control.hint
    HoverHandler { id: hoverHandler }
}
