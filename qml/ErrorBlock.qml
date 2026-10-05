import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: block
    property string message: ""
    property string code: ""
    property string details: ""
    property bool expanded: false
    onMessageChanged: expanded = false
    visible: message.length > 0
    spacing: 6
    Label {
        textFormat: Text.PlainText
        text: block.code
        visible: text.length > 0
        color: Theme.error
        font.family: Theme.monoFont
        font.pixelSize: 11
        Layout.fillWidth: true
        wrapMode: Text.Wrap
    }
    Label {
        textFormat: Text.PlainText
        text: block.message
        color: Theme.ink
        Layout.fillWidth: true
        wrapMode: Text.Wrap
    }
    AppButton {
        text: block.expanded ? "Hide details" : "Show details"
        visible: block.details.length > 0
        onClicked: block.expanded = !block.expanded
    }
    Label {
        textFormat: Text.PlainText
        text: block.details
        visible: block.expanded
        color: Theme.inkMuted
        font.family: Theme.monoFont
        font.pixelSize: 11
        Layout.fillWidth: true
        wrapMode: Text.Wrap
    }
}
