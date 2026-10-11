import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: settings
    readonly property var search: librarySearch
    spacing: 18

    component ValueRow: ColumnLayout {
        property string label: ""
        property string value: ""
        spacing: 5
        Label { text: parent.label; color: Theme.inkMuted; font.pixelSize: 12; Layout.fillWidth: true }
        Label { text: parent.value; color: Theme.ink; font.family: Theme.monoFont; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }
    }

    function openConnection() {
        connectionDialog.open()
    }

    Label {
        text: "Card search"
        color: Theme.ink
        font.family: Theme.contentFont
        font.pixelSize: 27
    }
    Label {
        text: "Search starts with exact, prefix, contains, and fuzzy matches. Semantic retrieval adds cards with similar meaning when the local model is ready."
        color: Theme.inkMuted
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }
    Switch {
        objectName: "semanticSearchEnabled"
        text: "Include semantic matches"
        checked: settings.search.semanticEnabled
        onToggled: settings.search.semanticEnabled = checked
    }
    ValueRow {
        visible: settings.search.semanticEnabled && settings.search.provider === "local"
        label: "Local model"
        value: settings.search.modelLabel + " / " + settings.search.resolvedProfile + (settings.search.resolvedProfile === "desktop" ? " (~110 MB)" : settings.search.resolvedProfile === "compact" ? " (~34 MB)" : "")
        Layout.fillWidth: true
    }
    ComboBox {
        objectName: "semanticSearchProfile"
        Layout.fillWidth: true
        Layout.maximumWidth: 360
        visible: settings.search.semanticEnabled && settings.search.provider === "local"
        enabled: visible
        model: ["Automatic", "Desktop model (~110 MB)", "Compact model (~34 MB)"]
        currentIndex: settings.search.localProfile === "desktop" ? 1 : settings.search.localProfile === "compact" ? 2 : 0
        onActivated: settings.search.localProfile = ["automatic", "desktop", "compact"][currentIndex]
        ToolTip.visible: hovered
        ToolTip.text: "Choose the local semantic model profile"
    }
    ValueRow {
        visible: settings.search.semanticEnabled && settings.search.provider === "local"
        label: "Model status"
        value: settings.search.downloading ? "Downloading " + Math.round(settings.search.downloadProgress * 100) + "%" : settings.search.status || (settings.search.modelReady ? "Ready" : "Not prepared")
        Layout.fillWidth: true
    }
    ProgressBar {
        visible: settings.search.semanticEnabled && settings.search.provider === "local" && settings.search.downloading
        value: settings.search.downloadProgress
        Layout.fillWidth: true
    }
    Flow {
        Layout.fillWidth: true
        spacing: 10
        AppButton {
            visible: settings.search.semanticEnabled && settings.search.provider === "local"
            enabled: visible
            text: settings.search.downloading ? "Cancel download" : settings.search.modelReady ? "Prepare again" : "Prepare local model"
            onClicked: settings.search.downloading ? settings.search.cancelDownload() : settings.search.prepareModel()
        }
        AppButton {
            text: settings.search.provider === "remote" ? "Remote provider" : "Local model"
            onClicked: providerMenu.open()
            Menu {
                id: providerMenu
                MenuItem { text: "Local model"; onTriggered: settings.search.provider = "local" }
                MenuItem { text: "Remote encoding service"; onTriggered: settings.search.provider = "remote" }
            }
        }
    }
    ValueRow {
        visible: settings.search.provider === "remote"
        label: "Remote service"
        value: settings.search.remoteEndpoint + " / " + settings.search.remoteModel
        Layout.fillWidth: true
    }
    Label {
        visible: settings.search.provider === "remote"
        text: settings.search.useLanguageModelKey ? (settings.search.hasApiKey ? "Using the existing language model key for this configured service." : "A language model key is required by this service.") : "Search sends text only to the configured encoding service."
        color: Theme.inkMuted
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }
    ErrorBlock {
        message: settings.search.error
        Layout.fillWidth: true
    }

    Dialog {
        id: connectionDialog
        modal: true
        title: "Search connection"
        standardButtons: Dialog.Cancel | Dialog.Save
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(560, settings.width - 24)
        property alias endpointText: endpoint.text
        property alias modelText: model.text
        onOpened: {
            endpoint.text = settings.search.remoteEndpoint
            model.text = settings.search.remoteModel
            reuseKey.checked = settings.search.useLanguageModelKey
            serviceKey.text = ""
        }
        onAccepted: {
            settings.search.provider = "remote"
            settings.search.setRemoteConfiguration(endpoint.text, model.text, reuseKey.checked, serviceKey.text)
        }
        background: Rectangle { color: Theme.surface; border.color: Theme.rule; radius: 8 }
        contentItem: ScrollView {
            implicitHeight: Math.min(440, availableHeight)
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: connectionDialog.contentItem.availableWidth
                spacing: 12
                Label {
                text: "Semantic search can send the query and both card faces to this configured service. Use only a provider you trust."
                color: Theme.inkMuted
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                }
                Label { text: "Endpoint"; color: Theme.inkMuted }
                TextField { id: endpoint; Layout.fillWidth: true; placeholderText: "https://example.invalid/embeddings" }
                Label { text: "Embedding model"; color: Theme.inkMuted }
                TextField { id: model; Layout.fillWidth: true; placeholderText: "text-embedding model" }
                CheckBox { id: reuseKey; text: "Use the existing language model key" }
                TextField {
                    id: serviceKey
                    Layout.fillWidth: true
                    echoMode: TextInput.Password
                    placeholderText: settings.search.hasApiKey ? "Leave empty to keep the stored key" : "Provider API key"
                    visible: !reuseKey.checked
                }
                ValueRow { label: "Key storage"; value: settings.search.keyStorageStatus; Layout.fillWidth: true }
                AppButton {
                    text: "Clear stored embedding key"
                    visible: settings.search.hasApiKey && !reuseKey.checked
                    onClicked: settings.search.clearApiKey()
                }
            }
        }
    }
}
