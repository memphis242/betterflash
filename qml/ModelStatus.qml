import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: control
    property string text: ""
    state: "missing"
    readonly property color stateColor: {
        switch (state) {
        case "ready": return Theme.success;
        case "degraded": return Theme.warning;
        case "missing": return Theme.error;
        case "disabled":
        case "busy": return Theme.info;
        default: return Theme.inkMuted;
        }
    }
    spacing: 10
    Rectangle {
        implicitWidth: 9
        implicitHeight: 9
        radius: 4.5
        color: Theme.transparent
        border.width: 2
        border.color: control.stateColor
        Accessible.name: control.state
        ToolTip.visible: statusHover.hovered
        ToolTip.text: control.text
        HoverHandler { id: statusHover }
    }
    Label {
        text: control.text
        textFormat: Text.PlainText
        color: control.stateColor
        font.pixelSize: 17
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }
}
