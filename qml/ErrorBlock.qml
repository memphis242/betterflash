import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: block
    property int textSize: 13
    property string message: ""
    property string code: ""
    property string details: ""
    property bool expanded: false
    onMessageChanged: expanded = false
    visible: message.length > 0
    spacing: 6
    TextEdit {
        id: codeText
        textFormat: TextEdit.PlainText
        text: block.code
        visible: text.length > 0
        color: Theme.error
        font.family: Theme.monoFont
        font.pixelSize: block.textSize
        Layout.fillWidth: true
        wrapMode: TextEdit.Wrap
        selectByMouse: true
        readOnly: true
        cursorVisible: false
        activeFocusOnPress: true
        selectionColor: Theme.accentSoft
        selectedTextColor: Theme.ink
        Layout.preferredHeight: Math.max(contentHeight, font.pixelSize * 1.35)
    }
    TextEdit {
        id: messageText
        textFormat: TextEdit.PlainText
        font.pixelSize: block.textSize
        text: block.message
        color: Theme.ink
        Layout.fillWidth: true
        wrapMode: TextEdit.Wrap
        selectByMouse: true
        readOnly: true
        cursorVisible: false
        activeFocusOnPress: true
        selectionColor: Theme.accentSoft
        selectedTextColor: Theme.ink
        Layout.preferredHeight: Math.max(contentHeight, font.pixelSize * 1.35)
    }
    AppButton {
        font.pixelSize: block.textSize
        text: block.expanded ? "Hide details" : "Show details"
        visible: block.details.length > 0
        onClicked: block.expanded = !block.expanded
    }
    TextEdit {
        id: detailsText
        textFormat: TextEdit.PlainText
        text: block.details
        visible: block.expanded
        color: Theme.inkMuted
        font.family: Theme.monoFont
        font.pixelSize: block.textSize
        Layout.fillWidth: true
        wrapMode: TextEdit.Wrap
        selectByMouse: true
        readOnly: true
        cursorVisible: false
        activeFocusOnPress: true
        selectionColor: Theme.accentSoft
        selectedTextColor: Theme.ink
        Layout.preferredHeight: Math.max(contentHeight, font.pixelSize * 1.35)
    }
}
