import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: settingsPage
    required property var ui
    property int section: ui.settingsSection()
    onSectionChanged: ui.setSettingsSection(section)
    readonly property var sections: ["Appearance", "Keyboard", "Voice", "Language model", "Sync", "Data"]
    component ValueRow: ColumnLayout {
        property string label: ""
        property string value: ""
        spacing: 5
        Label {
            textFormat: Text.PlainText
            text: parent.label
            color: Theme.inkMuted
            font.pixelSize: 12
            Layout.fillWidth: true
        }
        Label {
            textFormat: Text.PlainText
            text: parent.value
            color: Theme.ink
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            font.family: Theme.monoFont
            font.pixelSize: 12
        }
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: settingsPage.ui.gutter
        spacing: 16
        RowLayout {
            objectName: "settingsTopBar"
            Layout.fillWidth: true
            spacing: 10
            ComboBox {
                objectName: "settingsSection"
                model: settingsPage.sections
                currentIndex: settingsPage.section
                onActivated: settingsPage.section = currentIndex
                Layout.fillWidth: true
                Layout.maximumWidth: settingsPage.ui.width < 600 ? 200 : 260
                ToolTip.visible: hovered
                ToolTip.text: "Settings section"
            }
            Item {
                Layout.fillWidth: true
            }
            AppButton {
                objectName: "settingsPrimary"
                visible: settingsPage.section >= 2
                text: settingsPage.section === 5 ? "Export" : settingsPage.ui.width < 600 ? "Edit" : "Edit connection"
                primary: true
                onClicked: {
                    switch (settingsPage.section) {
                    case 2:
                        settingsPage.ui.openVoiceConfig()
                        break
                    case 3:
                        settingsPage.ui.openAiConfig()
                        break
                    case 4:
                        settingsPage.ui.openSyncConfig()
                        break
                    case 5:
                        settingsPage.ui.exportData()
                        break
                    }
                }
            }
        }
        ScrollView {
            id: settingsScroll
            objectName: "settingsScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: settingsScroll.availableWidth
                spacing: 20
                ColumnLayout {
                    visible: settingsPage.section === 0
                    Layout.fillWidth: true
                    spacing: 16
                    Label {
                        textFormat: Text.PlainText
                        text: "Appearance"
                        color: Theme.ink
                        font.family: Theme.contentFont
                        font.pixelSize: 27
                    }
                    ValueRow {
                        label: "Theme"
                        value: Theme.dark ? "Aubergine" : "Paper"
                        Layout.fillWidth: true
                    }
                    AppButton {
                        text: Theme.dark ? "Switch to paper" : "Switch to aubergine"
                        onClicked: Theme.toggle()
                    }
                    Label {
                        textFormat: Text.PlainText
                        text: "Your theme choice is remembered on this device."
                        color: Theme.inkMuted
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Switch {
                        text: "Reduce motion"
                        checked: Theme.reducedMotion
                        onToggled: Theme.preferences.reducedMotion = checked
                        ToolTip.visible: hovered
                        ToolTip.text: "Move directly to the active card without animating the review queue."
                    }
                }
                ColumnLayout {
                    visible: settingsPage.section === 1
                    Layout.fillWidth: true
                    spacing: 12
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            textFormat: Text.PlainText
                            text: "Key bindings"
                            color: Theme.ink
                            font.family: Theme.contentFont
                            font.pixelSize: 27
                            Layout.fillWidth: true
                        }
                        AppButton {
                            objectName: "resetShortcuts"
                            visible: shortcuts.customized
                            text: "Reset bindings"
                            onClicked: shortcuts.reset()
                        }
                    }
                    Label {
                        textFormat: Text.PlainText
                        text: "Select a command to change its key combination. Review keys are active when a review has focus; editor keys apply while writing a card."
                        color: Theme.inkMuted
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Repeater {
                        model: settingsPage.ui.shortcutCommands
                        delegate: ItemDelegate {
                            required property var modelData
                            Layout.fillWidth: true
                            height: settingsPage.ui.width < 600 ? 64 : 46
                            onClicked: settingsPage.ui.openRebind(modelData.action)
                            contentItem: RowLayout {
                                spacing: 14
                                Label {
                                    textFormat: Text.PlainText
                                    text: modelData.label
                                    color: Theme.ink
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                                Label {
                                    textFormat: Text.PlainText
                                    text: shortcuts.bindings[modelData.action] || ""
                                    color: Theme.inkMuted
                                    font.family: Theme.monoFont
                                    font.pixelSize: 11
                                    Layout.preferredWidth: settingsPage.ui.width < 600 ? 100 : 150
                                    horizontalAlignment: Text.AlignRight
                                }
                            }
                            background: Rectangle {
                                color: parent.hovered || parent.activeFocus ? Theme.accentSoft : Theme.transparent
                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    width: parent.width
                                    height: 1
                                    color: Theme.rule
                                }
                            }
                        }
                    }
                }
                ColumnLayout {
                    visible: settingsPage.section === 2
                    Layout.fillWidth: true
                    spacing: 18
                    Label {
                        textFormat: Text.PlainText
                        text: "Voice review"
                        color: Theme.ink
                        font.family: Theme.contentFont
                        font.pixelSize: 27
                    }
                    Switch {
                        objectName: "voiceEnabled"
                        text: "Enable voice review"
                        checked: voice.enabled
                        onToggled: voice.enabled = checked
                    }
                    ValueRow {
                        label: "Speech recognition"
                        value: voice.sttProvider === "groq" ? "Groq / " + voice.sttModel : "Local test / " + voice.modelPath
                        Layout.fillWidth: true
                    }
                    ValueRow {
                        label: "Playback"
                        value: "System text-to-speech"
                        Layout.fillWidth: true
                    }
                    Label {
                        textFormat: Text.PlainText
                        text: "With Groq selected, microphone audio is sent to Groq while voice review is enabled. Questions, answers, grades, deferring, postponing and pausing can be spoken."
                        color: Theme.inkMuted
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 10
                        AppButton {
                            text: voice.hasApiKey ? "Replace Groq key" : "Add Groq key"
                            onClicked: settingsPage.ui.openCredentials("voice")
                        }
                        AppButton {
                            visible: voice.hasApiKey
                            text: "Clear key"
                            onClicked: voice.clearApiKey()
                        }
                    }
                    ValueRow {
                        label: "Credential storage"
                        value: voice.keyStorageStatus
                        Layout.fillWidth: true
                    }
                    ValueRow {
                        label: "Status"
                        value: voice.status
                        Layout.fillWidth: true
                    }
                    ErrorBlock {
                        message: voice.lastError
                        Layout.fillWidth: true
                    }
                    Label {
                        textFormat: Text.PlainText
                        text: "Say 'start review', 'show answer', 'good', 'next card', 'previous card', 'defer', 'pause', or 'resume'. Partial recall accepts 'two of three points'. Use 'confirm rating' or 'keep rating' when correcting an earlier result."
                        color: Theme.inkMuted
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                        font.pixelSize: 12
                    }
                }
                ColumnLayout {
                    visible: settingsPage.section === 3
                    Layout.fillWidth: true
                    spacing: 18
                    Label {
                        textFormat: Text.PlainText
                        text: "Language model"
                        color: Theme.ink
                        font.family: Theme.contentFont
                        font.pixelSize: 27
                    }
                    ValueRow {
                        label: "Endpoint"
                        value: ai.endpoint
                        Layout.fillWidth: true
                    }
                    ValueRow {
                        label: "Model"
                        value: ai.model
                        Layout.fillWidth: true
                    }
                    Label {
                        textFormat: Text.PlainText
                        text: "A remaining-card summary is generated only when you request it during a review. Before generating, you can see which provider receives the question prompts."
                        color: Theme.inkMuted
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 10
                        AppButton {
                            text: ai.hasApiKey ? "Replace API key" : "Add API key"
                            onClicked: settingsPage.ui.openCredentials("ai")
                        }
                        AppButton {
                            text: "Clear key"
                            visible: ai.hasApiKey
                            onClicked: ai.clearApiKey()
                        }
                    }
                    ValueRow {
                        label: "Credential storage"
                        value: ai.keyStorageStatus
                        Layout.fillWidth: true
                    }
                    ErrorBlock {
                        message: ai.error
                        Layout.fillWidth: true
                    }
                }
                ColumnLayout {
                    visible: settingsPage.section === 4
                    Layout.fillWidth: true
                    spacing: 18
                    Label {
                        textFormat: Text.PlainText
                        text: "Device sync"
                        color: Theme.ink
                        font.family: Theme.contentFont
                        font.pixelSize: 27
                    }
                    ValueRow {
                        label: "Server"
                        value: sync.endpoint || "No server configured"
                        Layout.fillWidth: true
                    }
                    ValueRow {
                        label: "Last sync"
                        value: sync.lastSync ? settingsPage.ui.dateTime(sync.lastSync) : "No completed sync"
                        Layout.fillWidth: true
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 10
                        AppButton {
                            objectName: "synchronize"
                            text: sync.busy ? "Syncing..." : "Synchronize now"
                            enabled: !sync.busy && !!sync.endpoint
                            onClicked: sync.synchronize()
                        }
                        AppButton {
                            text: sync.hasToken ? "Replace token" : "Add token"
                            onClicked: settingsPage.ui.openCredentials("sync")
                        }
                        AppButton {
                            text: "Clear token"
                            visible: sync.hasToken
                            onClicked: sync.clearToken()
                        }
                    }
                    ValueRow {
                        label: "Credential storage"
                        value: sync.keyStorageStatus
                        Layout.fillWidth: true
                    }
                    Label {
                        textFormat: Text.PlainText
                        text: sync.status
                        color: Theme.inkMuted
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    ErrorBlock {
                        message: sync.error
                        Layout.fillWidth: true
                    }
                }
                ColumnLayout {
                    visible: settingsPage.section === 5
                    Layout.fillWidth: true
                    spacing: 18
                    Label {
                        textFormat: Text.PlainText
                        text: "Collection data"
                        color: Theme.ink
                        font.family: Theme.contentFont
                        font.pixelSize: 27
                    }
                    ValueRow {
                        label: "Local collection"
                        value: app.storagePath
                        Layout.fillWidth: true
                    }
                    Label {
                        textFormat: Text.PlainText
                        text: "Export a collection backup, or import cards and review records from another BetterFlash collection."
                        color: Theme.inkMuted
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    AppButton {
                        text: "Import collection"
                        onClicked: settingsPage.ui.importData()
                    }
                    AppButton {
                        visible: app.decks.length === 0
                        objectName: "loadExamples"
                        text: "Add example deck"
                        onClicked: app.loadExampleDeck()
                    }
                }
            }
        }
    }
}
