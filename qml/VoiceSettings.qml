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
    readonly property string ttsState: localTts && voice.ttsModelDownloading ? "busy"
        : !voice.ttsReady ? "missing" : voice.ttsTestError ? "degraded" : "ready"

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
                model: ["Whisper large v3 turbo / desktop (874 MB download)", "Vosk English 0.22 / balanced (131 MB download)", "Vosk small English 0.15 / compact (41 MB download)"]
                currentIndex: voice.sttProfile === "whisper-turbo" ? 0 : voice.sttProfile === "small-en" ? 2 : 1
                onActivated: voice.sttProfile = ["whisper-turbo", "balanced-en", "small-en"][currentIndex]
                ToolTip.visible: hovered
                ToolTip.text: "Choose the desktop Whisper profile, balanced Vosk recognition, or the compact profile intended for Android"
            }
            Label { text: "Speech recognition acceleration"; color: Theme.inkMuted; font.pixelSize: 15; Layout.fillWidth: true; visible: sttProvider.currentIndex === 0 }
            SettingsCombo {
                Layout.fillWidth: true
                visible: sttProvider.currentIndex === 0
                model: ["Automatic (GPU when available, CPU fallback)", "CPU"]
                currentIndex: voice.sttAcceleration === "cpu" ? 1 : 0
                onActivated: voice.sttAcceleration = currentIndex === 1 ? "cpu" : "automatic"
                ToolTip.visible: hovered
                ToolTip.text: "Automatic uses a Vulkan GPU when the desktop helper supports it and retries on CPU if GPU inference cannot run"
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
                    enabled: !voice.modelTestBusy && (!root.ai || !root.ai.testBusy)
                        && (!voice.customSttTests || (!voice.customSttTests.busy && !voice.customSttTests.recording))
                        && (voice.sttProvider === "groq" || voice.sttModelReady)
                    hint: enabled ? "Run a short speech recognition check" : "Download the local model before testing"
                    onClicked: voice.testSttModel()
                }
            }
            Label { text: "Recognition vocabulary"; color: Theme.inkMuted; font.pixelSize: 15; Layout.fillWidth: true }
            TextArea {
                Layout.fillWidth: true
                Layout.preferredHeight: 84
                text: voice.recognitionVocabulary
                placeholderText: "Optional terms, one per line or separated by commas"
                wrapMode: TextEdit.Wrap
                font.pixelSize: 16
                leftPadding: 12
                rightPadding: 12
                topPadding: 10
                bottomPadding: 10
                color: Theme.ink
                placeholderTextColor: Theme.inkMuted
                background: Rectangle {
                    radius: 6
                    color: Theme.editorField
                    border.color: parent.activeFocus ? Theme.accent : Theme.rule
                    border.width: parent.activeFocus ? 2 : 1
                }
                onTextChanged: if (activeFocus) voice.recognitionVocabulary = text
                ToolTip.visible: hovered
                ToolTip.text: "Terms are supplied as context to local Whisper so project and technical abbreviations are more likely to be recognized"
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: "Correct transcripts with local language model"; color: Theme.ink; font.pixelSize: 16; Layout.fillWidth: true; wrapMode: Text.Wrap }
                SettingsSwitch {
                    checked: voice.transcriptCorrector.enabled
                    onToggled: voice.transcriptCorrector.enabled = checked
                    ToolTip.visible: hovered
                    ToolTip.text: "After speech recognition, use the selected local language model to repair grammar and likely technical abbreviations"
                }
            }
            ModelStatus {
                Layout.fillWidth: true
                visible: voice.transcriptCorrector.enabled
                state: voice.transcriptCorrector.ready ? "ready" : voice.transcriptCorrector.error ? "degraded" : "missing"
                text: voice.transcriptCorrector.ready ? "Local transcript correction is ready" : voice.transcriptCorrector.error ? voice.transcriptCorrector.error : "Prepare the selected local language model to enable transcript correction"
            }
            ModelTestResult {
                Layout.fillWidth: true
                title: "Speech recognition test"
                report: voice.sttTestReport
                status: voice.sttTestStatus
                error: voice.sttTestError
                busy: voice.sttTestBusy
            }
            CustomSttTests {
                Layout.fillWidth: true
                voice: root.voice
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
