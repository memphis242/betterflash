import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property var voice
    property var ai: null
    property string apiKeyDraft: ""
    property int tabIndex: 0
    onTabIndexChanged: if (tabIndex !== 0) clearSecretDraft()
    spacing: 18

    component SettingsButton: AppButton { font.pixelSize: 16 }
    component SettingsCombo: ComboBox { font.pixelSize: 17 }
    component SettingsSwitch: AppSwitch { font.pixelSize: 16 }
    component SettingsError: ErrorBlock { textSize: 16 }
    component ValueRow: ColumnLayout {
        property string label: ""
        property string value: ""
        spacing: 5
        Label {
            textFormat: Text.PlainText
            text: parent.label
            color: Theme.inkMuted
            font.pixelSize: 15
            Layout.fillWidth: true
        }
        Label {
            textFormat: Text.PlainText
            text: parent.value
            color: Theme.ink
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            font.family: Theme.monoFont
            font.pixelSize: 17
        }
    }

    function clearSecretDraft() { apiKeyDraft = "" }
    readonly property bool localStt: voice.sttProvider !== "groq"
    readonly property bool localTts: voice.ttsProvider !== "system"
    readonly property string sttState: voice.sttModelDownloading ? "busy" : !localStt ? "disabled" : voice.sttModelReady && !voice.sttTestError ? "ready" : voice.sttModelReady ? "degraded" : "missing"
    readonly property string ttsState: voice.ttsModelDownloading ? "busy" : !localTts ? "disabled" : voice.ttsReady && !voice.ttsTestError ? "ready" : voice.ttsReady ? "degraded" : "missing"

    SettingsSwitch {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        objectName: "voiceEnabled"
        text: "Enable voice review"
        checked: voice.enabled
        onToggled: voice.enabled = checked
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 4
        Repeater {
            model: ["Speech recognition", "Speech output"]
            delegate: Button {
                required property int index
                required property string modelData
                Layout.fillWidth: true
                implicitHeight: 48
                flat: true
                text: modelData
                font.family: Theme.uiFont
                font.pixelSize: 16
                contentItem: Row {
                    spacing: 8
                    anchors.centerIn: parent
                    Icon {
                        kind: index === 0 ? "ear" : "mouth-sound"
                        width: 24
                        height: 24
                        stroke: root.tabIndex === index ? Theme.accent : Theme.inkMuted
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Label {
                        text: modelData
                        color: root.tabIndex === index ? Theme.accent : Theme.inkMuted
                        font: parent.parent.font
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                background: Rectangle {
                    color: root.tabIndex === index ? Theme.accentSoft : Theme.transparent
                    radius: 5
                    Rectangle {
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: 2
                        color: root.tabIndex === index ? Theme.accent : Theme.rule
                    }
                }
                onClicked: root.tabIndex = index
                ToolTip.visible: hovered
                ToolTip.text: index === 0 ? "Configure speech recognition" : "Configure speech output"
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true

        ColumnLayout {
            visible: root.tabIndex === 0
            spacing: 14
            Layout.fillWidth: true

            Label { text: "Speech recognition"; color: Theme.inkMuted; font.pixelSize: 15 }
            SettingsCombo {
                id: sttProvider
                Layout.fillWidth: true
                model: ["Local model", "Groq cloud"]
                currentIndex: voice.sttProvider === "groq" ? 1 : 0
                onActivated: {
                    voice.sttProvider = currentIndex === 1 ? "groq" : "local"
                    if (currentIndex !== 1)
                        root.clearSecretDraft()
                }
                ToolTip.visible: hovered
                ToolTip.text: "Choose on-device speech recognition or Groq"
            }
            TextField {
                Layout.fillWidth: true
                visible: sttProvider.currentIndex === 1
                text: voice.sttModel
                onEditingFinished: voice.sttModel = text.trim()
                placeholderText: "whisper-large-v3-turbo"
                font.pixelSize: 17
            }
            TextField {
                Layout.fillWidth: true
                visible: sttProvider.currentIndex === 0
                text: voice.modelPath
                onEditingFinished: voice.modelPath = text.trim()
                placeholderText: "Local model folder"
                font.pixelSize: 17
            }
            SettingsCombo {
                Layout.fillWidth: true
                visible: sttProvider.currentIndex === 0
                model: ["Vosk small English (41 MB download)"]
                currentIndex: 0
                onActivated: voice.sttProfile = "small-en"
                ToolTip.visible: hovered
                ToolTip.text: "Choose the local speech recognition profile"
            }
            ModelStatus {
                visible: sttProvider.currentIndex === 0
                Layout.fillWidth: true
                text: voice.sttModelStatus
                state: root.sttState
            }
            ProgressBar {
                visible: sttProvider.currentIndex === 0 && voice.sttModelDownloading
                value: voice.sttModelProgress
                Layout.fillWidth: true
            }
            Flow {
                Layout.fillWidth: true
                spacing: 10
                ModelActionButton {
                    visible: sttProvider.currentIndex === 0
                    width: Math.min(implicitWidth, root.width)
                    actionKind: "download"
                    text: voice.sttModelDownloading ? "Cancel download" : voice.sttModelReady ? "Download again" : "Download local model"
                    enabled: true
                    onClicked: voice.sttModelDownloading ? voice.cancelSttDownload() : voice.downloadSttModel()
                }
                ModelActionButton {
                    width: Math.min(implicitWidth, root.width)
                    actionKind: "test"
                    text: voice.sttTestBusy ? "Testing..." : "Test model"
                    enabled: !voice.modelTestBusy && (!root.ai || !root.ai.testBusy) && (voice.sttProvider === "groq" || voice.sttModelReady)
                    hint: enabled ? "Run a short speech recognition check" : "Download the local model before testing"
                    onClicked: voice.testSttModel()
                }
            }
            ModelTestResult {
                Layout.fillWidth: true
                title: "Speech recognition test"
                report: voice.sttTestReport
                status: voice.sttTestStatus
                error: voice.sttTestError
                busy: voice.sttTestBusy
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 10
                visible: sttProvider.currentIndex === 1
                TextField {
                    Layout.fillWidth: true
                    echoMode: TextInput.Password
                    text: root.apiKeyDraft
                    placeholderText: voice.hasApiKey ? "Leave empty to keep the stored Groq key" : "Groq API key"
                    font.pixelSize: 17
                    onTextChanged: if (activeFocus) root.apiKeyDraft = text
                }
                SettingsButton {
                    text: "Save key"
                    enabled: root.apiKeyDraft.trim().length > 0
                    onClicked: { voice.setApiKey(root.apiKeyDraft.trim()); root.apiKeyDraft = "" }
                }
                SettingsButton {
                    visible: voice.hasApiKey
                    text: "Clear stored key"
                    onClicked: voice.clearApiKey()
                }
            }
        }

        ColumnLayout {
            visible: root.tabIndex === 1
            spacing: 14
            Layout.fillWidth: true

            Label { text: "Speech output"; color: Theme.inkMuted; font.pixelSize: 15 }
            SettingsCombo {
                id: ttsProvider
                Layout.fillWidth: true
                model: ["Local Piper voice", "System speech engine"]
                currentIndex: voice.ttsProvider === "system" ? 1 : 0
                onActivated: voice.ttsProvider = currentIndex === 1 ? "system" : "local"
                ToolTip.visible: hovered
                ToolTip.text: "Use a local Piper voice, or an installed system speech engine"
            }
            Label { text: "Text-to-speech profile"; visible: ttsProvider.currentIndex === 0; color: Theme.inkMuted; font.pixelSize: 15 }
            SettingsCombo {
                Layout.fillWidth: true
                visible: ttsProvider.currentIndex === 0
                model: ["Amy low / compact (67 MB download)", "LJSpeech medium (67 MB download)"]
                currentIndex: voice.ttsProfile === "ljspeech-medium" ? 1 : 0
                onActivated: voice.ttsProfile = ["amy-low", "ljspeech-medium"][currentIndex]
                ToolTip.visible: hovered
                ToolTip.text: "Choose a local speech synthesis profile"
            }
            ModelStatus {
                Layout.fillWidth: true
                text: voice.ttsStatus
                state: root.ttsState
            }
            ProgressBar {
                visible: ttsProvider.currentIndex === 0 && voice.ttsModelDownloading
                value: voice.ttsModelProgress
                Layout.fillWidth: true
            }
            Flow {
                Layout.fillWidth: true
                spacing: 10
                ModelActionButton {
                    visible: ttsProvider.currentIndex === 0
                    width: Math.min(implicitWidth, root.width)
                    actionKind: "download"
                    text: voice.ttsModelDownloading ? "Cancel download" : voice.ttsReady ? "Download again" : "Download local model"
                    onClicked: voice.ttsModelDownloading ? voice.cancelTtsDownload() : voice.downloadTtsModel()
                }
                ModelActionButton {
                    width: Math.min(implicitWidth, root.width)
                    actionKind: "test"
                    text: voice.ttsTestBusy ? "Testing..." : "Test model"
                    enabled: !voice.modelTestBusy && (!root.ai || !root.ai.testBusy) && (voice.ttsProvider === "system" || voice.ttsReady)
                    hint: enabled ? "Run a short speech synthesis check" : "Download the local model before testing"
                    onClicked: voice.testTtsModel()
                }
            }
            ModelTestResult {
                Layout.fillWidth: true
                title: "Speech output test"
                report: voice.ttsTestReport
                status: voice.ttsTestStatus
                error: voice.ttsTestError
                busy: voice.ttsTestBusy
            }
        }
    }

    ValueRow {
        visible: voice.sttProvider === "groq"
        label: "Credential storage"
        value: voice.keyStorageStatus
        Layout.fillWidth: true
    }
    ModelStatus {
        state: !voice.enabled ? "disabled" : voice.lastError ? "degraded" : voice.ready ? "ready" : "busy"
        text: voice.status
        Layout.fillWidth: true
    }
    SettingsError { message: voice.lastError; Layout.fillWidth: true }
    Label {
        textFormat: Text.PlainText
        text: "Say 'start review', 'show answer', 'good', 'next card', 'previous card', 'defer', 'pause', or 'resume'. Partial recall accepts 'two of three points'."
        color: Theme.inkMuted
        wrapMode: Text.Wrap
        Layout.fillWidth: true
        font.pixelSize: 16
    }
}
