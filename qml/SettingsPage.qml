import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: settingsPage
    required property var ui
    component SettingsButton: AppButton { font.pixelSize: 16 }
    component SettingsCombo: ComboBox { font.pixelSize: 17 }
    component SettingsSwitch: AppSwitch { font.pixelSize: 16 }
    component SettingsError: ErrorBlock { textSize: 16 }

    property int section: ui.settingsSection()
    onSectionChanged: {
        ui.setSettingsSection(section)
        if (section !== 2) voiceKeyDraft = ""
        if (section !== 3) aiKeyDraft = ""
        if (section !== 4) syncTokenDraft = ""
    }
    readonly property var sections: ["Appearance", "Keyboard", "Voice", "Language model", "Sync", "Data", "Search", "Code rendering"]
    readonly property var sectionTitles: ["Appearance", "Key bindings", "Voice review", "Language model", "Device sync", "Collection data", "Library search", "Code rendering"]
    property var sectionScrollPositions: ({})
    property string aiEndpointDraft: ai.endpoint
    property string aiModelDraft: ai.model
    property string voiceKeyDraft: ""
    property string aiKeyDraft: ""
    property string syncTokenDraft: ""
    property string exportDestinationDraft: ""
    onVisibleChanged: if (!visible) {
        voiceKeyDraft = ""
        aiKeyDraft = ""
        syncTokenDraft = ""
    }

    Connections {
        target: ai
        function onConfigurationChanged() {
            if (!aiEndpointField.activeFocus) settingsPage.aiEndpointDraft = ai.endpoint;
            if (!aiModelField.activeFocus) settingsPage.aiModelDraft = ai.model;
        }
    }

    function selectSection(index) {
        if (index < 0 || index >= sections.length || index === section)
            return;
        const positions = Object.assign({}, sectionScrollPositions);
        positions[section] = settingsScroll.contentItem.contentY;
        sectionScrollPositions = positions;
        section = index;
        Qt.callLater(function () {
            if (settingsPage.section !== index)
                return;
            const viewport = settingsScroll.contentItem;
            viewport.contentY = Math.max(0, Math.min(settingsPage.sectionScrollPositions[index] || 0, viewport.contentHeight - viewport.height));
        });
    }

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
    RowLayout {
        anchors.fill: parent
        anchors.margins: settingsPage.ui.gutter
        spacing: settingsPage.width < 650 ? 12 : 24

        ListView {
            id: sectionList
            objectName: "settingsSections"
            Layout.fillHeight: true
            Layout.preferredWidth: Math.max(108, Math.min(196, settingsPage.width * 0.23))
            Layout.minimumWidth: Layout.preferredWidth
            Layout.maximumWidth: Layout.preferredWidth
            model: settingsPage.sections
            currentIndex: settingsPage.section
            spacing: 6
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }

            function moveSelection(index) {
                const next = Math.max(0, Math.min(count - 1, index));
                settingsPage.selectSection(next);
                positionViewAtIndex(next, ListView.Contain);
                const item = itemAtIndex(next);
                if (item)
                    item.forceActiveFocus();
            }

            delegate: ItemDelegate {
                id: sectionButton
                required property int index
                required property string modelData
                readonly property bool selected: settingsPage.section === index
                width: sectionList.width
                implicitHeight: Math.max(50, sectionLabel.implicitHeight + 24)
                leftPadding: 14
                rightPadding: 12
                topPadding: 12
                bottomPadding: 12
                text: modelData
                Accessible.name: text
                Accessible.description: "Settings section"
                onClicked: settingsPage.selectSection(index)
                Keys.onUpPressed: sectionList.moveSelection(index - 1)
                Keys.onDownPressed: sectionList.moveSelection(index + 1)
                Keys.onPressed: event => {
                    switch (event.key) {
                    case Qt.Key_Home:
                        sectionList.moveSelection(0);
                        event.accepted = true;
                        break;
                    case Qt.Key_End:
                        sectionList.moveSelection(sectionList.count - 1);
                        event.accepted = true;
                        break;
                    }
                }
                contentItem: Label {
                    id: sectionLabel
                    text: sectionButton.text
                    textFormat: Text.PlainText
                    font.family: Theme.uiFont
                    font.pixelSize: settingsPage.width < 650 ? 13 : 15
                    font.weight: sectionButton.selected ? Font.DemiBold : Font.Normal
                    color: sectionButton.selected ? Theme.accent : Theme.inkMuted
                    wrapMode: Text.Wrap
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 6
                    color: sectionButton.selected || sectionButton.hovered ? Theme.accentSoft : Theme.transparent
                    border.width: sectionButton.activeFocus ? 2 : 0
                    border.color: Theme.accent
                    Rectangle {
                        visible: sectionButton.selected
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: 3
                        height: parent.height - 18
                        radius: 1.5
                        color: Theme.accent
                    }
                }
            }
        }
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            color: Theme.rule
        }
        ColumnLayout {
            id: settingsContent
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 0
            spacing: 20
            RowLayout {
                objectName: "settingsTopBar"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 12
                Label {
                    text: settingsPage.sectionTitles[settingsPage.section] || "Settings"
                    textFormat: Text.PlainText
                    color: Theme.ink
                    font.family: Theme.contentFont
                    font.pixelSize: settingsPage.width < 650 ? 22 : 27
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    elide: Text.ElideRight
                    ToolTip.visible: truncated && titleHover.hovered
                    ToolTip.text: text
                    HoverHandler {
                        id: titleHover
                    }
                }
                SettingsButton {
                    objectName: "resetShortcuts"
                    visible: settingsPage.section === 1 && shortcuts.customized
                    text: settingsPage.width < 720 ? "Reset" : "Reset bindings"
                    hint: "Reset key bindings"
                    Layout.maximumWidth: Math.max(76, settingsContent.width * 0.45)
                    onClicked: shortcuts.reset()
                }
            }
            ScrollView {
                id: settingsScroll
                objectName: "settingsScroll"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 0
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: settingsScroll.availableWidth
                    spacing: 20
                    ColumnLayout {
                        visible: settingsPage.section === 0
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 16
                        ValueRow {
                            label: "Theme"
                            value: Theme.dark ? "Aubergine" : "Paper"
                            Layout.fillWidth: true
                        }
                        SettingsButton {
                            Layout.maximumWidth: settingsContent.width
                            text: Theme.dark ? "Switch to paper" : "Switch to aubergine"
                            onClicked: Theme.toggle()
                        }
                        Label {
                            textFormat: Text.PlainText
                            text: "Your theme choice is remembered on this device."
                            color: Theme.inkMuted
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                            font.pixelSize: 16
                        }
                        SettingsSwitch {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
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
                        Layout.minimumWidth: 0
                        spacing: 12
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
                                Layout.minimumWidth: 0
                                height: settingsContent.width < 520 ? 76 : 46
                                onClicked: settingsPage.ui.openRebind(modelData.action)
                                contentItem: RowLayout {
                                    spacing: 14
                                    Label {
                                        textFormat: Text.PlainText
                                        text: modelData.label
                                        color: Theme.ink
                                        wrapMode: Text.Wrap
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                    }
                                    Label {
                                        textFormat: Text.PlainText
                                        text: shortcuts.bindings[modelData.action] || ""
                                        color: Theme.inkMuted
                                        font.family: Theme.monoFont
                                        font.pixelSize: 11
                                        Layout.minimumWidth: 0
                                        Layout.preferredWidth: Math.min(150, settingsContent.width * 0.38)
                                        horizontalAlignment: Text.AlignRight
                                        wrapMode: Text.Wrap
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
                        Layout.minimumWidth: 0
                        spacing: 18
                        SettingsSwitch {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            objectName: "voiceEnabled"
                            text: "Enable voice review"
                            checked: voice.enabled
                            onToggled: voice.enabled = checked
                        }
                        ValueRow {
                            label: "Speech recognition"
                            value: voice.sttProvider === "groq" ? "Groq / " + voice.sttModel : "Local / " + voice.sttProfile
                            Layout.fillWidth: true
                        }
                        Label {
                            text: "Speech recognition"
                            color: Theme.inkMuted
                            font.pixelSize: 15
                        }
                        SettingsCombo {
                            id: sttProviderInline
                            Layout.fillWidth: true
                            model: ["Local model", "Groq cloud"]
                            currentIndex: voice.sttProvider === "groq" ? 1 : 0
                            onActivated: voice.sttProvider = currentIndex === 1 ? "groq" : "local"
                            ToolTip.visible: hovered
                            ToolTip.text: "Choose on-device speech recognition or Groq"
                        }
                        TextField {
                            Layout.fillWidth: true
                            visible: sttProviderInline.currentIndex === 1
                            text: voice.sttModel
                            onEditingFinished: voice.sttModel = text.trim()
                            placeholderText: "whisper-large-v3-turbo"
                            font.pixelSize: 17
                        }
                        TextField {
                            Layout.fillWidth: true
                            visible: sttProviderInline.currentIndex === 0
                            text: voice.modelPath
                            onEditingFinished: voice.modelPath = text.trim()
                            placeholderText: "Local model folder"
                            font.pixelSize: 17
                        }
                        SettingsCombo {
                            Layout.fillWidth: true
                            visible: sttProviderInline.currentIndex === 0
                            model: ["Automatic compact", "Small English"]
                            currentIndex: voice.sttProfile === "small-en" ? 1 : 0
                            onActivated: voice.sttProfile = currentIndex === 1 ? "small-en" : "automatic"
                            ToolTip.visible: hovered
                            ToolTip.text: "Choose the local speech recognition profile"
                        }
                        ValueRow {
                            visible: sttProviderInline.currentIndex === 0
                            label: "Speech model status"
                            value: voice.sttModelStatus
                            Layout.fillWidth: true
                        }
                        ProgressBar {
                            visible: sttProviderInline.currentIndex === 0 && voice.sttModelDownloading
                            value: voice.sttModelProgress
                            Layout.fillWidth: true
                        }
                        SettingsButton {
                            visible: sttProviderInline.currentIndex === 0
                            text: voice.sttModelDownloading ? "Cancel download" : voice.sttModelReady ? "Download again" : "Download local speech model"
                            onClicked: voice.sttModelDownloading ? voice.cancelSttDownload() : voice.downloadSttModel()
                        }
                        SettingsButton {
                            text: voice.sttTestBusy ? "Testing..." : voice.sttProvider === "local" && !voice.sttModelReady ? "Download and test" : "Test model"
                            enabled: !voice.modelTestBusy
                            onClicked: voice.testSttModel()
                        }
                        ValueRow {
                            label: "Recognition check"
                            value: voice.sttTestStatus
                            visible: !!voice.sttTestStatus
                            Layout.fillWidth: true
                        }
                        SettingsError { message: voice.sttTestError; Layout.fillWidth: true }
                        ValueRow {
                            label: "Playback"
                            value: voice.ttsProvider === "system" ? "System speech engine" : "Piper on this device"
                            Layout.fillWidth: true
                        }
                        Label {
                            visible: voice.sttProvider === "groq"
                            textFormat: Text.PlainText
                            text: "With Groq selected, microphone audio is sent to Groq while voice review is enabled. Questions, answers, grades, deferring, postponing and pausing can be spoken."
                            color: Theme.inkMuted
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                            font.pixelSize: 16
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            visible: sttProviderInline.currentIndex === 1
                            TextField {
                                Layout.fillWidth: true
                                echoMode: TextInput.Password
                                text: settingsPage.voiceKeyDraft
                                placeholderText: voice.hasApiKey ? "Leave empty to keep the stored Groq key" : "Groq API key"
                                font.pixelSize: 17
                                onTextChanged: if (activeFocus) settingsPage.voiceKeyDraft = text
                            }
                            SettingsButton {
                                text: "Save key"
                                enabled: settingsPage.voiceKeyDraft.trim().length > 0
                                onClicked: {
                                    voice.setApiKey(settingsPage.voiceKeyDraft.trim())
                                    settingsPage.voiceKeyDraft = ""
                                }
                            }
                            SettingsButton {
                                visible: voice.hasApiKey
                                text: "Clear stored key"
                                onClicked: voice.clearApiKey()
                            }
                        }
                        Label { text: "Text-to-speech provider"; color: Theme.inkMuted; font.pixelSize: 15 }
                        SettingsCombo {
                            Layout.fillWidth: true
                            model: ["Local Piper voice", "System speech engine"]
                            currentIndex: voice.ttsProvider === "system" ? 1 : 0
                            onActivated: voice.ttsProvider = currentIndex === 1 ? "system" : "local"
                            ToolTip.visible: hovered
                            ToolTip.text: "Use the bundled local voice runtime, or an installed system speech engine"
                        }
                        Label { text: "Text-to-speech profile"; visible: voice.ttsProvider === "local"; color: Theme.inkMuted; font.pixelSize: 15 }
                        SettingsCombo {
                            Layout.fillWidth: true
                            visible: voice.ttsProvider === "local"
                            model: ["Amy low / compact (67 MB download)", "LJSpeech medium (67 MB download)"]
                            currentIndex: voice.ttsProfile === "ljspeech-medium" ? 1 : 0
                            onActivated: voice.ttsProfile = ["amy-low", "ljspeech-medium"][currentIndex]
                            ToolTip.visible: hovered
                            ToolTip.text: "Choose a local speech synthesis profile"
                        }
                        ValueRow {
                            label: "Speech output status"
                            value: voice.ttsStatus
                            Layout.fillWidth: true
                        }
                        ProgressBar {
                            visible: voice.ttsProvider === "local" && voice.ttsModelDownloading
                            value: voice.ttsModelProgress
                            Layout.fillWidth: true
                        }
                        SettingsButton {
                            visible: voice.ttsProvider === "local"
                            text: voice.ttsModelDownloading ? "Cancel download" : voice.ttsReady ? "Download again" : "Download local speech model"
                            onClicked: voice.ttsModelDownloading ? voice.cancelTtsDownload() : voice.downloadTtsModel()
                        }
                        SettingsButton {
                            text: voice.ttsTestBusy ? "Testing..." : voice.ttsProvider === "local" && !voice.ttsReady ? "Download and test" : "Test model"
                            enabled: !voice.modelTestBusy
                            onClicked: voice.testTtsModel()
                        }
                        ValueRow {
                            label: "Speech output check"
                            value: voice.ttsTestStatus
                            visible: !!voice.ttsTestStatus
                            Layout.fillWidth: true
                        }
                        SettingsError {
                            message: voice.ttsTestError
                            Layout.fillWidth: true
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
                        SettingsError {
                            message: voice.lastError
                            Layout.fillWidth: true
                        }
                        Label {
                            textFormat: Text.PlainText
                            text: "Say 'start review', 'show answer', 'good', 'next card', 'previous card', 'defer', 'pause', or 'resume'. Partial recall accepts 'two of three points'. Use 'confirm rating' or 'keep rating' when correcting an earlier result."
                            color: Theme.inkMuted
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                            font.pixelSize: 16
                        }
                    }
                    ColumnLayout {
                        visible: settingsPage.section === 3
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 18
                        Label { text: "Provider"; color: Theme.inkMuted; font.pixelSize: 15 }
                        SettingsCombo {
                            id: aiProvider
                            Layout.fillWidth: true
                            model: ["Local model", "Remote provider"]
                            currentIndex: ai.provider === "remote" ? 1 : 0
                            onActivated: if (typeof ai.provider !== "undefined") ai.provider = currentIndex === 1 ? "remote" : "local"
                            ToolTip.visible: hovered
                            ToolTip.text: "Local models work offline. Remote providers require a key."
                        }
                        Label { visible: aiProvider.currentIndex === 0; text: "Local profile"; color: Theme.inkMuted; font.pixelSize: 15 }
                        SettingsCombo {
                            visible: aiProvider.currentIndex === 0
                            Layout.fillWidth: true
                            model: ["Automatic", "Gemma 4 E2B", "LFM 2.5 350M"]
                            currentIndex: ai.localProfile === "lfm" ? 2 : ai.localProfile === "gemma" ? 1 : 0
                            onActivated: if (typeof ai.localProfile !== "undefined") ai.localProfile = ["automatic", "gemma", "lfm"][currentIndex]
                        }
                        Label { visible: aiProvider.currentIndex === 0; text: "Acceleration"; color: Theme.inkMuted; font.pixelSize: 15 }
                        SettingsCombo {
                            visible: aiProvider.currentIndex === 0
                            Layout.fillWidth: true
                            model: ["Automatic", "CPU"]
                            currentIndex: ai.acceleration === "cpu" ? 1 : 0
                            onActivated: if (typeof ai.acceleration !== "undefined") ai.acceleration = ["automatic", "cpu"][currentIndex]
                            ToolTip.visible: hovered
                            ToolTip.text: "Use an available GPU automatically, or force CPU execution"
                        }
                        ValueRow {
                            visible: aiProvider.currentIndex === 0
                            label: "Local model"
                            value: ai.localModelLabel || ai.resolvedProfile || "Automatic hardware profile"
                            Layout.fillWidth: true
                        }
                        ProgressBar {
                            visible: aiProvider.currentIndex === 0 && ai.localDownloading
                            value: ai.localDownloadProgress
                            Layout.fillWidth: true
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            SettingsButton {
                                visible: aiProvider.currentIndex === 0
                                text: ai.localDownloading ? "Cancel download" : ai.localModelReady ? "Prepare again" : "Prepare local model"
                                onClicked: ai.localDownloading ? ai.cancelLocalDownload() : ai.prepareLocalModel()
                            }
                            SettingsButton {
                                text: ai.testBusy ? "Testing..." : ai.provider === "local" && !ai.localModelReady ? "Download and test" : "Test model"
                                enabled: !ai.testBusy
                                onClicked: if (typeof ai.testModel === "function") ai.testModel()
                            }
                        }
                        ValueRow {
                            visible: aiProvider.currentIndex === 0
                            label: "Local status"
                            value: ai.localStatus || "Ready to prepare"
                            Layout.fillWidth: true
                        }
                        ValueRow {
                            visible: aiProvider.currentIndex === 0
                            label: "Execution device"
                            value: ai.acceleratorStatus
                            Layout.fillWidth: true
                        }
                        SettingsError {
                            visible: aiProvider.currentIndex === 0
                            message: ai.localError
                            Layout.fillWidth: true
                        }
                        Label { visible: aiProvider.currentIndex === 1; text: "Remote endpoint"; color: Theme.inkMuted; font.pixelSize: 15 }
                        TextField {
                            id: aiEndpointField
                            visible: aiProvider.currentIndex === 1
                            Layout.fillWidth: true
                            text: settingsPage.aiEndpointDraft
                            onTextChanged: if (activeFocus) settingsPage.aiEndpointDraft = text
                            onEditingFinished: ai.endpoint = settingsPage.aiEndpointDraft.trim()
                            font.pixelSize: 17
                            placeholderText: "https://api.groq.com/openai/v1/chat/completions"
                        }
                        Label { visible: aiProvider.currentIndex === 1; text: "Remote model"; color: Theme.inkMuted; font.pixelSize: 15 }
                        TextField {
                            id: aiModelField
                            visible: aiProvider.currentIndex === 1
                            Layout.fillWidth: true
                            text: settingsPage.aiModelDraft
                            onTextChanged: if (activeFocus) settingsPage.aiModelDraft = text
                            onEditingFinished: ai.model = settingsPage.aiModelDraft.trim()
                            font.pixelSize: 17
                            placeholderText: "Provider model identifier"
                        }
                        Label {
                            textFormat: Text.PlainText
                            text: "A remaining-card summary is generated only when you request it during a review. Before generating, you can see which provider receives the question prompts."
                            color: Theme.inkMuted
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            font.pixelSize: 16
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            visible: aiProvider.currentIndex === 1
                            TextField {
                                Layout.fillWidth: true
                                echoMode: TextInput.Password
                                text: settingsPage.aiKeyDraft
                                placeholderText: ai.hasApiKey ? "Leave empty to keep the stored API key" : "Provider API key"
                                font.pixelSize: 17
                                onTextChanged: if (activeFocus) settingsPage.aiKeyDraft = text
                            }
                            SettingsButton {
                                text: "Save key"
                                enabled: settingsPage.aiKeyDraft.trim().length > 0
                                onClicked: {
                                    ai.setApiKey(settingsPage.aiKeyDraft.trim())
                                    settingsPage.aiKeyDraft = ""
                                }
                            }
                            SettingsButton {
                                text: "Clear key"
                                visible: ai.hasApiKey
                                onClicked: ai.clearApiKey()
                            }
                        }
                        ValueRow {
                            label: "Model test"
                            value: ai.testStatus
                            visible: !!ai.testStatus
                            Layout.fillWidth: true
                        }
                        SettingsError {
                            message: ai.testError
                            Layout.fillWidth: true
                        }
                        ValueRow {
                            label: "Credential storage"
                            value: ai.keyStorageStatus
                            Layout.fillWidth: true
                        }
                        SettingsError {
                            message: ai.error
                            Layout.fillWidth: true
                        }
                    }
                    ColumnLayout {
                        visible: settingsPage.section === 4
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 18
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
                        Label { text: "Server address"; color: Theme.inkMuted; font.pixelSize: 15 }
                        TextField {
                            id: syncEndpointInline
                            Layout.fillWidth: true
                            text: sync.endpoint
                            onEditingFinished: sync.endpoint = text.trim()
                            placeholderText: "https://your-server.example"
                            font.pixelSize: 17
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            SettingsButton {
                                width: Math.min(implicitWidth, parent.width)
                                objectName: "synchronize"
                                text: sync.busy ? "Syncing..." : "Synchronize now"
                                enabled: !sync.busy && !!sync.endpoint
                                onClicked: sync.synchronize()
                            }
                            SettingsButton {
                                text: "Save connection"
                                primary: true
                                onClicked: sync.endpoint = syncEndpointInline.text.trim()
                            }
                            TextField {
                                Layout.fillWidth: true
                                echoMode: TextInput.Password
                                text: settingsPage.syncTokenDraft
                                placeholderText: sync.hasToken ? "Leave empty to keep the stored token" : "Sync access token"
                                font.pixelSize: 17
                                onTextChanged: if (activeFocus) settingsPage.syncTokenDraft = text
                            }
                            SettingsButton {
                                text: "Save token"
                                enabled: settingsPage.syncTokenDraft.trim().length > 0
                                onClicked: {
                                    sync.setToken(settingsPage.syncTokenDraft.trim())
                                    settingsPage.syncTokenDraft = ""
                                }
                            }
                            SettingsButton {
                                width: Math.min(implicitWidth, parent.width)
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
                        SettingsError {
                            message: sync.error
                            Layout.fillWidth: true
                        }
                    }
                    ColumnLayout {
                        visible: settingsPage.section === 5
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 18
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
                            font.pixelSize: 16
                        }
                        Label { text: "Export destination"; color: Theme.inkMuted; font.pixelSize: 15 }
                        TextField {
                            Layout.fillWidth: true
                            text: settingsPage.exportDestinationDraft
                            onTextChanged: if (activeFocus) settingsPage.exportDestinationDraft = text
                            placeholderText: "Choose a .json file"
                            font.pixelSize: 17
                        }
                        SettingsButton {
                            text: "Export collection"
                            primary: true
                            onClicked: settingsPage.ui.exportData(settingsPage.exportDestinationDraft)
                        }
                        SettingsButton {
                            Layout.maximumWidth: settingsContent.width
                            text: "Import collection"
                            onClicked: settingsPage.ui.importData()
                        }
                        SettingsButton {
                            Layout.maximumWidth: settingsContent.width
                            visible: app.decks.length === 0
                            objectName: "loadExamples"
                            text: "Add example deck"
                            onClicked: app.loadExampleDeck()
                        }
                    }
                    SearchSettings {
                        id: searchSettings
                        visible: settingsPage.section === 6
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                    }
                    CodeRenderingSettings {
                        visible: settingsPage.section === 7
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                    }
                }
            }
        }
    }
}
