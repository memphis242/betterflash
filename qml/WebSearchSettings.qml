import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: settings
    required property var service
    readonly property bool dialogOpen: keyEditor.dialogOpen
    spacing: 16

    onVisibleChanged: if (!visible) keyEditor.clearSecretDraft()

    AppSwitch {
        font.pixelSize: 16
        objectName: "webSearchEnabled"
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        text: "Allow web search during card checks"
        checked: settings.service.enabled
        onToggled: settings.service.enabled = checked
    }

    Label {
        textFormat: Text.PlainText
        text: "Correctness and completeness checks are available using your configured language model even when web search is off. Web search is disabled by default. When enabled, the model can make up to three basic Tavily searches based on the card content and receive result snippets to help check current facts. Searches use your Tavily account quota."
        color: Theme.inkMuted
        wrapMode: Text.Wrap
        Layout.fillWidth: true
        font.pixelSize: 16
    }

    AppButton {
        text: "Get a Tavily API key"
        hint: "Open Tavily's site to create or manage a search API key"
        Layout.alignment: Qt.AlignLeft
        onClicked: Qt.openUrlExternally("https://app.tavily.com")
    }

    ApiKeyEditor {
        id: keyEditor
        binding: settings.service.apiKey
        credentialLabel: "Tavily API key"
        clearAction: "Clear key"
        Layout.fillWidth: true
        Layout.minimumWidth: 0
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 10
        ModelActionButton {
            actionKind: "test"
            text: settings.service.testBusy ? "Testing..." : "Test connection"
            enabled: !settings.service.testBusy && settings.service.hasApiKey
            hint: settings.service.hasApiKey
                ? "Run one basic search with a fixed query. This uses one Tavily credit."
                : "Save a Tavily API key before testing the connection."
            onClicked: settings.service.testConnection()
        }
        AppButton {
            visible: settings.service.testBusy
            text: "Cancel test"
            hint: "Cancel the connection test; Tavily may still charge for a request already received."
            onClicked: settings.service.cancelTest()
        }
    }

    Label {
        text: "The connection test sends a fixed query and uses one Tavily credit. It works while web search is disabled."
        textFormat: Text.PlainText
        color: Theme.inkMuted
        wrapMode: Text.Wrap
        Layout.fillWidth: true
        font.pixelSize: 15
    }

    Label {
        visible: settings.service.testStatus.length > 0 && settings.service.testErrorCode.length === 0
        text: settings.service.testStatus
        textFormat: Text.PlainText
        color: Theme.ink
        wrapMode: Text.Wrap
        Layout.fillWidth: true
        font.pixelSize: 16
    }

    Rectangle {
        Layout.fillWidth: true
        visible: settings.service.testErrorCode.length > 0
        implicitHeight: Math.min(errorBlock.implicitHeight, 180) + 24
        radius: 6
        color: Theme.errorSurface
        border.color: Theme.error
        border.width: 1

        ScrollView {
            id: errorScroll
            anchors.fill: parent
            anchors.margins: 12
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical.policy: ScrollBar.AsNeeded

            ErrorBlock {
                id: errorBlock
                width: errorScroll.availableWidth
                textSize: 15
                message: settings.service.testErrorCode.length > 0 ? settings.service.testStatus : ""
                code: settings.service.testErrorCode
                details: settings.service.testErrorDetail
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        visible: settings.service.testInput.length > 0
        implicitHeight: Math.min(testTranscript.implicitHeight, 260) + 24
        color: Theme.surfaceRaised
        border.color: Theme.rule
        radius: 6

        ScrollView {
            id: transcriptScroll
            anchors.fill: parent
            anchors.margins: 12
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            ColumnLayout {
                id: testTranscript
                width: transcriptScroll.availableWidth
                spacing: 8
                Label {
                    text: "Test input"
                    color: Theme.ink
                    font.bold: true
                    font.pixelSize: 16
                }
                TextEdit {
                    text: settings.service.testInput
                    textFormat: TextEdit.PlainText
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    color: Theme.ink
                    font.family: Theme.uiFont
                    font.pixelSize: 16
                    Layout.fillWidth: true
                    Layout.preferredHeight: contentHeight
                }
                Label {
                    text: "Test result"
                    color: Theme.ink
                    font.bold: true
                    font.pixelSize: 16
                }
                TextEdit {
                    text: settings.service.testBusy ? "Waiting for Tavily..." : settings.service.testResult
                    textFormat: TextEdit.PlainText
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    color: Theme.ink
                    font.family: Theme.uiFont
                    font.pixelSize: 16
                    Layout.fillWidth: true
                    Layout.preferredHeight: contentHeight
                }
            }
        }
    }

    Label {
        textFormat: Text.PlainText
        text: "Search results can be incomplete, misleading, or out of date. The language model can also make mistakes when interpreting them. Check important facts against the linked sources."
        color: Theme.inkMuted
        wrapMode: Text.Wrap
        Layout.fillWidth: true
        font.pixelSize: 16
    }
}
