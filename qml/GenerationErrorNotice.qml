import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: notice
    property string message: ""
    property string operation: "generation"
    property bool expanded: false
    readonly property var parsed: /^([A-Z][A-Z0-9_]{1,80}):\s*([\s\S]*)$/.exec(message)
    readonly property string errorCode: parsed ? parsed[1] : ""
    readonly property string reason: parsed ? parsed[2] : message
    readonly property bool refused: errorCode === "CARD_GENERATION_REFUSED"
    readonly property string operationName: operation === "refinement" ? "refinement" : operation === "verification" ? "answer check" : "card generation"
    readonly property string summary: refused
        ? (operation === "generation" ? "The model declined to generate a card." : "The model declined this " + operationName + ".")
        : "The " + operationName + " could not be completed."
    readonly property string action: operation === "refinement"
        ? (refused ? "Revise the instructions and try again, or edit and accept the unchanged draft yourself."
                   : "Review the details and try again, or edit and accept the unchanged draft yourself.")
        : operation === "verification"
            ? "Review the details and try the answer check again. Your draft is unchanged."
            : (refused ? "Revise the instructions and try again, or create a card manually."
                       : "Review the details and try again, or create a card manually.")
    visible: message.length > 0
    implicitHeight: noticeContent.implicitHeight + 24
    color: Theme.surfaceRaised
    radius: 6
    border.width: 2
    border.color: Theme.error
    onMessageChanged: {
        expanded = false
        Qt.callLater(function () {
            reasonViewport.contentItem.contentY = 0
            detailViewport.contentItem.contentY = 0
        })
    }
    ScrollView {
        id: noticeViewport
        anchors.fill: parent
        anchors.margins: 12
        contentWidth: availableWidth
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ColumnLayout {
            id: noticeContent
            width: noticeViewport.availableWidth
            spacing: 6
        Label {
            text: notice.summary
            textFormat: Text.PlainText
            color: Theme.error
            font.pixelSize: 18
            font.bold: true
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            Accessible.role: Accessible.AlertMessage
            Accessible.name: text
        }
        ScrollView {
            id: reasonViewport
            visible: !!notice.reason
            Layout.fillWidth: true
            Layout.minimumHeight: visible ? 20 : 0
            Layout.preferredHeight: Math.min(80, reasonText.contentHeight + 2)
            contentWidth: availableWidth
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            TextEdit {
                id: reasonText
                width: reasonViewport.availableWidth
                text: notice.reason
                textFormat: TextEdit.PlainText
                readOnly: true
                selectByMouse: true
                cursorVisible: false
                wrapMode: TextEdit.Wrap
                color: Theme.ink
                font.family: Theme.uiFont
                font.pixelSize: 16
            }
        }
        Label {
            text: notice.action
            textFormat: Text.PlainText
            color: Theme.ink
            font.pixelSize: 15
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        AppButton {
            implicitHeight: 30
            font.pixelSize: 13
            text: notice.expanded ? "Hide details" : "Show details"
            onClicked: notice.expanded = !notice.expanded
        }
        ScrollView {
            id: detailViewport
            visible: notice.expanded
            Layout.fillWidth: true
            Layout.minimumHeight: visible ? 20 : 0
            Layout.preferredHeight: Math.min(70, errorDetails.implicitHeight)
            contentWidth: availableWidth
            clip: true
            ErrorBlock {
                id: errorDetails
                width: detailViewport.availableWidth
                message: notice.errorCode ? "Error code: " + notice.errorCode : "No error code was supplied."
                details: ""
                textSize: 13
            }
        }
        }
    }
}
