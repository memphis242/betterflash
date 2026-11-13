import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: block
    default property alias additionalContent: content.data
    property int textSize: 13
    property string message: ""
    property string action: ""
    property string code: ""
    property string details: ""
    readonly property var parsedMessage: /^([A-Z][A-Z0-9_]{1,80}):\s*([\s\S]*)$/.exec(message)
    readonly property string displayCode: code || (parsedMessage ? parsedMessage[1] : "")
    readonly property string plainMessage: parsedMessage ? parsedMessage[2] : message
    readonly property int remedyStart: action ? -1 : plainMessage.search(/(?:^|[.!?]\s+)(?=(?:Check|Replace|Try|Choose|Save|Open|Wait|Review|Configure|Enable|Select|Download|Finish|Reconnect|Retry|Run|Request|Update|Contact|Enter|Use)\b)/)
    readonly property string displayMessage: remedyStart > 0
        ? plainMessage.slice(0, remedyStart + 1).trim() : plainMessage
    readonly property string displayAction: action || (remedyStart > 0 ? plainMessage.slice(remedyStart + 1).trim() : "")
    property bool expanded: false
    onMessageChanged: expanded = false
    onDetailsChanged: expanded = false
    visible: message.length > 0
    implicitHeight: content.implicitHeight + 24
    color: Theme.errorSurface
    border.color: Theme.error
    radius: 6
    data: ColumnLayout {
        id: content
        x: 12; y: 12; width: Math.max(0, parent.width - 24)
        spacing: 8
        TextEdit {
            id: codeText
            textFormat: TextEdit.PlainText
            text: block.displayCode
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
            font.family: Theme.uiFont
            text: block.displayMessage
            color: Theme.error
            font.bold: true
            Accessible.role: Accessible.AlertMessage
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
            textFormat: TextEdit.PlainText
            text: block.displayAction
            visible: text.length > 0
            color: Theme.ink
            font.family: Theme.uiFont
            font.pixelSize: block.textSize
            Layout.fillWidth: true
            wrapMode: TextEdit.Wrap
            selectByMouse: true
            readOnly: true
            cursorVisible: false
            Layout.preferredHeight: contentHeight
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
}
