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

    Label {
        textFormat: Text.PlainText
        text: "Search results can be incomplete, misleading, or out of date. The language model can also make mistakes when interpreting them. Check important facts against the linked sources."
        color: Theme.inkMuted
        wrapMode: Text.Wrap
        Layout.fillWidth: true
        font.pixelSize: 16
    }
}
