import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: settings
    readonly property var search: librarySearch
    readonly property bool dialogOpen: embeddingKeyStatus.dialogOpen
    spacing: 18
    component SettingsButton: AppButton { font.pixelSize: 16 }
    component SettingsCombo: AppComboBox { font.pixelSize: 17 }
    component SettingsSwitch: AppSwitch { font.pixelSize: 16 }
    component SettingsError: ErrorBlock { textSize: 16 }


    component ValueRow: ColumnLayout {
        property string label: ""
        property string value: ""
        spacing: 5
        Label {
            text: parent.label
            color: Theme.inkMuted
            font.pixelSize: 15
            Layout.fillWidth: true
        }
        Label {
            text: parent.value
            color: Theme.ink
            font.family: Theme.monoFont
            font.pixelSize: 17
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
    }

    Label {
        text: "Search cards in the selected deck or across the library. Text matches appear first. Semantic search then adds cards with related meaning. Local models run on this device and download on first use."
        color: Theme.inkMuted
        font.pixelSize: 16
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }
    SettingsSwitch {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        objectName: "semanticSearchEnabled"
        text: "Include semantic matches"
        checked: settings.search.semanticEnabled
        onToggled: settings.search.semanticEnabled = checked
    }
    ModelSettingsSection {
        id: embeddingModelSection
        title: "Semantic retrieval model"
    ModelLocationPicker {
        id: embeddingLocation
        localHint: "Runs semantic retrieval on this machine using the selected embedding model."
        remoteHint: "Uses a cloud embedding endpoint. The configured endpoint, model, and key are required."
        currentIndex: settings.search.provider === "remote" ? 1 : 0
        onActivated: selectedIndex => settings.search.provider = selectedIndex === 1 ? "remote" : "local"
    }
    ValueRow {
        visible: settings.search.semanticEnabled && settings.search.provider === "local"
        label: "Local model"
        value: settings.search.modelLabel + " / " + settings.search.resolvedProfile + (settings.search.resolvedProfile === "desktop" ? " (~110 MB)" : settings.search.resolvedProfile === "compact" ? " (~34 MB)" : "")
        Layout.fillWidth: true
    }
    Label {
        visible: settings.search.provider === "remote"
        text: "Remote endpoint"
        color: Theme.inkMuted
        font.pixelSize: 15
    }
    TextField {
        id: remoteEndpoint
        visible: settings.search.provider === "remote"
        Layout.fillWidth: true
        text: settings.search.remoteEndpoint
        placeholderText: "https://example.invalid/embeddings"
        font.pixelSize: 17
    }
    Label {
        visible: settings.search.provider === "remote"
        text: "Embedding model"
        color: Theme.inkMuted
        font.pixelSize: 15
    }
    TextField {
        id: remoteModel
        visible: settings.search.provider === "remote"
        Layout.fillWidth: true
        text: settings.search.remoteModel
        placeholderText: "text-embedding model"
        font.pixelSize: 17
    }
    CheckBox {
        id: reuseKey
        visible: settings.search.provider === "remote"
        text: "Use the existing language model key"
        checked: settings.search.useLanguageModelKey
        font.pixelSize: 15
    }
    Label {
        visible: settings.search.provider === "remote" && !reuseKey.checked
        text: "Embedding API key"
        color: Theme.inkMuted
        font.pixelSize: 15
    }
    TextField {
        id: serviceKey
        visible: settings.search.provider === "remote" && !reuseKey.checked
        Layout.fillWidth: true
        echoMode: TextInput.Password
        placeholderText: settings.search.hasApiKey ? "Leave empty to keep the stored key" : "Provider API key"
        font.pixelSize: 17
        onVisibleChanged: if (!visible) text = ""
    }
    Flow {
        visible: settings.search.provider === "remote"
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.preferredWidth: 0
        spacing: 10
        SettingsButton {
            width: Math.min(implicitWidth, parent.width)
            text: "Save remote connection"
            primary: true
            onClicked: {
                if (settings.search.setRemoteConfiguration(remoteEndpoint.text.trim(), remoteModel.text.trim(), reuseKey.checked, serviceKey.text))
                    serviceKey.text = "";
            }
        }
        SettingsButton {
            width: Math.min(implicitWidth, parent.width)
            text: "Clear stored embedding key"
            visible: !settings.search.useLanguageModelKey
                && (settings.search.hasApiKey || settings.search.keyStorageState === "removal-failed")
            onClicked: settings.search.clearApiKey()
        }
        ApiKeyStatus {
            id: embeddingKeyStatus
            width: Math.min(implicitWidth, parent.width)
            storageState: settings.search.keyStorageState
            storageStatus: settings.search.keyStorageStatus
            hasKey: settings.search.hasApiKey
            reusesLanguageModelKey: settings.search.useLanguageModelKey
            saveAction: "Save remote connection"
            clearAction: "Clear stored embedding key"
        }
    }
    SettingsCombo {
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
    ModelStatus {
        visible: settings.search.provider === "local"
        state: !settings.search.semanticEnabled ? "disabled" : settings.search.downloading ? "busy"
            : settings.search.modelReady ? settings.search.testError ? "degraded" : "ready" : "missing"
        text: !settings.search.semanticEnabled ? "Semantic search disabled"
            : settings.search.downloading ? "Downloading " + Math.round(settings.search.downloadProgress * 100) + "%"
            : settings.search.modelReady ? settings.search.testError ? "Model files available; validation failed" : "Ready for offline use" : "Local model is not downloaded"
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
        ModelActionButton {
            width: Math.min(implicitWidth, parent.width)
            visible: settings.search.semanticEnabled && settings.search.provider === "local"
            enabled: visible
            actionKind: "download"
            text: settings.search.downloading ? "Cancel download" : settings.search.modelReady ? "Verify model files" : "Download local model"
            hint: "Download and verify the selected model files for offline search. Testing is a separate action."
            onClicked: settings.search.downloading ? settings.search.cancelDownload() : settings.search.prepareModel()
        }
        ModelActionButton {
            width: Math.min(implicitWidth, parent.width)
            actionKind: "test"
            text: settings.search.testBusy ? "Testing..." : "Test model"
            enabled: !settings.search.testBusy && (settings.search.provider === "remote" || settings.search.modelReady)
            hint: settings.search.testBusy ? "The model validation is running"
                : settings.search.provider === "local" && !settings.search.modelReady ? "Download the local model before testing"
                : "Compare embeddings for related and unrelated text without changing your cards"
            onClicked: settings.search.testModel()
        }
    }
    ModelTestResult {
        title: "Embedding model test"
        report: settings.search.testReport
        status: settings.search.testStatus
        error: settings.search.testError
        busy: settings.search.testBusy
        Layout.fillWidth: true
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
    SettingsError {
        message: settings.search.error
        Layout.fillWidth: true
    }

    }

    Flow {
        Layout.fillWidth: true
        spacing: 10
        SettingsButton {
            width: Math.min(implicitWidth, parent.width)
            text: "Rebuild search index"
            enabled: !settings.search.downloading
            hint: "Clear cached embeddings. The next search encodes cards in the active search scope again."
            onClicked: settings.search.rebuildIndex()
        }
    }
}
