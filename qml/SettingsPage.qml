import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: settingsPage
    required property var ui
    property int section: ui.settingsSection()
    onSectionChanged: ui.setSettingsSection(section)
    readonly property var sections: ["Appearance", "Keyboard", "Voice", "Language model", "Sync", "Data", "Search", "Code rendering"]
    readonly property var sectionTitles: ["Appearance", "Key bindings", "Voice review", "Language model", "Device sync", "Collection data", "Library search", "Code rendering"]
    property var sectionScrollPositions: ({})

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
                Keys.onHomePressed: sectionList.moveSelection(0)
                Keys.onEndPressed: sectionList.moveSelection(sectionList.count - 1)
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
                AppButton {
                    objectName: "resetShortcuts"
                    visible: settingsPage.section === 1 && shortcuts.customized
                    text: settingsPage.width < 720 ? "Reset" : "Reset bindings"
                    hint: "Reset key bindings"
                    Layout.maximumWidth: Math.max(76, settingsContent.width * 0.45)
                    onClicked: shortcuts.reset()
                }
                AppButton {
                    objectName: "settingsPrimary"
                    visible: settingsPage.section >= 2 && settingsPage.section <= 6
                    text: settingsPage.section === 5 ? "Export" : settingsPage.width < 720 ? "Edit" : "Edit connection"
                    hint: settingsPage.section === 5 ? "Export collection" : "Edit connection"
                    primary: true
                    Layout.maximumWidth: Math.max(76, settingsContent.width * 0.45)
                    onClicked: {
                        switch (settingsPage.section) {
                        case 2:
                            settingsPage.ui.openVoiceConfig();
                            break;
                        case 3:
                            settingsPage.ui.openAiConfig();
                            break;
                        case 4:
                            settingsPage.ui.openSyncConfig();
                            break;
                        case 5:
                            settingsPage.ui.exportData();
                            break;
                        case 6:
                            searchSettings.openConnection();
                            break;
                        }
                    }
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
                        AppButton {
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
                        }
                        AppSwitch {
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
                        AppSwitch {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
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
                                width: Math.min(implicitWidth, parent.width)
                                text: voice.hasApiKey ? "Replace Groq key" : "Add Groq key"
                                onClicked: settingsPage.ui.openCredentials("voice")
                            }
                            AppButton {
                                width: Math.min(implicitWidth, parent.width)
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
                        Layout.minimumWidth: 0
                        spacing: 18
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
                                width: Math.min(implicitWidth, parent.width)
                                text: ai.hasApiKey ? "Replace API key" : "Add API key"
                                onClicked: settingsPage.ui.openCredentials("ai")
                            }
                            AppButton {
                                width: Math.min(implicitWidth, parent.width)
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
                        Flow {
                            Layout.fillWidth: true
                            spacing: 10
                            AppButton {
                                width: Math.min(implicitWidth, parent.width)
                                objectName: "synchronize"
                                text: sync.busy ? "Syncing..." : "Synchronize now"
                                enabled: !sync.busy && !!sync.endpoint
                                onClicked: sync.synchronize()
                            }
                            AppButton {
                                width: Math.min(implicitWidth, parent.width)
                                text: sync.hasToken ? "Replace token" : "Add token"
                                onClicked: settingsPage.ui.openCredentials("sync")
                            }
                            AppButton {
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
                        ErrorBlock {
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
                        }
                        AppButton {
                            Layout.maximumWidth: settingsContent.width
                            text: "Import collection"
                            onClicked: settingsPage.ui.importData()
                        }
                        AppButton {
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
