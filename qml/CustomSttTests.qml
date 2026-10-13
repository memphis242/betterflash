import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ColumnLayout {
    id: root
    required property var voice
    readonly property var tests: voice.customSttTests
    property var expectedDrafts: ({})
    property string importSlotId: ""
    readonly property bool draftSetReady: tests.entries.some(entry => entry.hasRecording)
        && tests.entries.filter(entry => entry.hasRecording).every(entry => root.expectedFor(entry).trim().length > 0)
    readonly property bool providerReady: voice.sttProvider === "groq" ? voice.hasApiKey : voice.sttModelReady && !voice.sttModelDownloading
    Layout.fillWidth: true
    spacing: 10
    onVisibleChanged: if (!visible && tests.recording) tests.stopRecording()

    function expectedFor(entry) {
        const key = String(entry.id)
        return Object.prototype.hasOwnProperty.call(expectedDrafts, key) ? expectedDrafts[key] : String(entry.expected || "")
    }
    function rememberExpected(id, value) {
        const next = Object.assign({}, expectedDrafts)
        next[String(id)] = String(value)
        expectedDrafts = next
    }
    function commitDraft(id) {
        if (Object.prototype.hasOwnProperty.call(expectedDrafts, String(id))) tests.setExpected(id, expectedDrafts[String(id)])
    }
    function commitAllDrafts() { for (const entry of tests.entries) commitDraft(entry.id) }

    FileDialog {
        id: recordingPicker
        title: "Choose a speech recording"
        fileMode: FileDialog.OpenFile
        nameFilters: ["WAV recordings (*.wav)", "All files (*)"]
        onAccepted: { if (root.importSlotId) tests.replaceRecording(root.importSlotId, selectedFile); root.importSlotId = "" }
    }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: customColumn.implicitHeight + 28
        color: Theme.surfaceRaised
        border.color: Theme.rule
        border.width: 1
        radius: 8

        ColumnLayout {
            id: customColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 14
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                Label { text: "Custom tests"; color: Theme.ink; font.family: Theme.uiFont; font.pixelSize: 18; font.bold: true }
                Label { text: "Five recording slots"; color: Theme.inkMuted; font.pixelSize: 14; Layout.fillWidth: true }
                Rectangle {
                    id: listeningStatus
                    visible: tests.recording
                    implicitWidth: listeningLabel.implicitWidth + 32
                    implicitHeight: 30
                    radius: 15
                    color: Theme.surfaceRaised
                    border.color: tests.recordingPaused ? Theme.warning : Theme.success
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 9; anchors.rightMargin: 9
                        spacing: 6
                        Rectangle { width: 8; height: 8; radius: 4; color: tests.recordingPaused ? Theme.warning : Theme.success }
                        Label {
                            id: listeningLabel
                            text: tests.recordingPaused ? "Microphone paused" : "Microphone listening"
                            color: tests.recordingPaused ? Theme.warning : Theme.success
                            font.pixelSize: 12; font.bold: true
                        }
                    }
                    HoverHandler { id: listeningHover }
                    ToolTip.visible: listeningHover.hovered
                    ToolTip.text: tests.recordingPaused ? "Recording is paused. Resume or stop to save this slot." : "The microphone is active for this slot."
                }
            }
            Label {
                Layout.fillWidth: true
                text: "Update each recording and expected transcription separately. Run an individual slot or test the populated set below."
                color: Theme.inkMuted; wrapMode: Text.Wrap; font.pixelSize: 14
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                Repeater {
                    model: tests.entries
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        implicitHeight: row.implicitHeight + 18
                        color: Theme.editorField; border.color: Theme.rule; border.width: 1; radius: 6
                        ColumnLayout {
                            id: row
                            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 9; spacing: 10
                            RowLayout {
                                id: fields
                                Layout.fillWidth: true
                                spacing: 10
                                ColumnLayout {
                                    Layout.preferredWidth: 170; Layout.alignment: Qt.AlignTop
                                    Label { text: "Slot " + (index + 1); color: Theme.ink; font.pixelSize: 15; font.bold: true }
                                    AppButton {
                                        text: modelData.hasRecording ? "Update recording" : "Add recording"
                                        glyph: "mic"; glyphSize: 16
                                        enabled: !tests.busy && !tests.recording && !voice.modelTestBusy
                                        hint: "Import a WAV or record this slot with the microphone"
                                        onClicked: updateMenu.open()
                                        AppMenu {
                                            id: updateMenu
                                            AppMenuItem { glyph: "folder"; text: "Import WAV"; enabled: !tests.recording; onTriggered: { root.commitAllDrafts(); root.importSlotId = modelData.id; recordingPicker.open() } }
                                            AppMenuItem { glyph: "mic"; text: "Record microphone"; enabled: !tests.recording; onTriggered: { root.commitAllDrafts(); tests.startRecordingForSlot(modelData.id) } }
                                            AppMenuItem { glyph: "empty-box"; text: "Clear recording"; enabled: modelData.hasRecording && !tests.recording; onTriggered: tests.removeRecording(modelData.id) }
                                        }
                                    }
                                    Label { Layout.fillWidth: true; text: modelData.hasRecording ? ((modelData.label || "Recording") + (Number(modelData.duration) > 0 ? "  " + Number(modelData.duration).toFixed(1) + " s" : "")) : "Empty slot"; color: Theme.inkMuted; elide: Text.ElideMiddle; font.family: Theme.monoFont; font.pixelSize: 12 }
                                    RowLayout {
                                        visible: tests.recording && tests.currentRecordingId === modelData.id
                                        Layout.fillWidth: true
                                        GlyphButton {
                                            glyph: tests.recordingPaused ? "play" : "pause"
                                            glyphSize: 17
                                            hint: tests.recordingPaused ? "Resume microphone capture" : "Pause microphone capture"
                                            onClicked: tests.recordingPaused ? tests.resumeRecording() : tests.pauseRecording()
                                        }
                                        Label {
                                            text: tests.recordingPaused ? "Resume" : "Pause"
                                            color: Theme.inkMuted; font.pixelSize: 12
                                        }
                                        GlyphButton {
                                            glyph: "stop"; glyphSize: 17
                                            hint: "Stop recording and save this slot"
                                            onClicked: tests.stopRecording()
                                        }
                                        Label { text: "Stop and save"; color: Theme.inkMuted; font.pixelSize: 12 }
                                    }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignTop
                                    spacing: 4
                                    Label {
                                        text: "Expected transcription"
                                        color: Theme.inkMuted; font.pixelSize: 12
                                        horizontalAlignment: Text.AlignHCenter
                                        Layout.fillWidth: true
                                    }
                                    TextArea {
                                        Layout.fillWidth: true; Layout.preferredHeight: 74
                                        text: root.expectedFor(modelData); placeholderText: "Enter the expected words"
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        font.pixelSize: 15; wrapMode: TextArea.Wrap; selectByMouse: true
                                        readOnly: tests.busy || tests.recording
                                        color: readOnly ? Theme.inkMuted : Theme.ink
                                        placeholderTextColor: Theme.inkMuted
                                        background: Rectangle {
                                            radius: 6
                                            color: parent.activeFocus ? Theme.surface : Theme.editorField
                                            border.color: parent.activeFocus ? Theme.accent : Theme.rule
                                            border.width: parent.activeFocus ? 2 : 1
                                        }
                                        onTextChanged: if (activeFocus) root.rememberExpected(modelData.id, text)
                                        onActiveFocusChanged: if (!activeFocus) root.commitDraft(modelData.id)
                                        Accessible.name: "Expected transcription for slot " + (index + 1)
                                        ToolTip.visible: hovered && !activeFocus
                                        ToolTip.text: readOnly ? "Expected transcription is locked while recording or testing" : "Click to edit the expected transcription"
                                    }
                                }
                                ColumnLayout {
                                Layout.preferredWidth: 145; Layout.alignment: Qt.AlignTop
                                ModelActionButton {
                                    text: "Test slot"
                                    actionKind: "test"
                                    enabled: modelData.hasRecording && root.expectedFor(modelData).trim().length > 0 && root.providerReady && !tests.busy && !tests.recording && !voice.modelTestBusy
                                    hint: "Transcribe only this recording"
                                    onClicked: { root.commitDraft(modelData.id); tests.runTest(modelData.id) }
                                }
                            }
                            }
                            Label {
                                visible: tests.recording && tests.currentRecordingId === modelData.id
                                Layout.fillWidth: true
                                text: "Recording activity  " + Number(tests.recordingDuration).toFixed(1) + " s"
                                color: tests.recordingPaused ? Theme.warning : Theme.success
                                font.family: Theme.monoFont; font.pixelSize: 12
                            }
                            Row {
                                visible: tests.recording && tests.currentRecordingId === modelData.id
                                Layout.fillWidth: true
                                height: 34
                                spacing: 2
                                Repeater {
                                    model: tests.recordingActivity
                                    delegate: Rectangle {
                                        required property real modelData
                                        width: Math.max(2, (parent.width - (tests.recordingActivity.length - 1) * 2) / Math.max(1, tests.recordingActivity.length))
                                        height: Math.max(3, Math.min(30, Math.sqrt(Math.max(0, modelData)) * 30))
                                        anchors.verticalCenter: parent.verticalCenter
                                        radius: 2
                                        color: modelData > 0.03 ? (tests.recordingPaused ? Theme.warning : Theme.success) : Theme.rule
                                        opacity: tests.recordingPaused ? 0.65 : 0.9
                                    }
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                Label { text: modelData.hasRecording ? (modelData.status || "Ready") : "Need recording"; color: modelData.status === "Passed" ? Theme.success : modelData.status === "Failed" ? Theme.error : Theme.inkMuted; font.pixelSize: 13 }
                                Label { visible: Boolean(modelData.rawTranscript || modelData.transcript); text: "Raw output: " + (modelData.rawTranscript || modelData.transcript || ""); color: Theme.inkMuted; wrapMode: Text.Wrap; font.pixelSize: 13; Layout.fillWidth: true }
                                Label { visible: Boolean(modelData.correctedTranscript); text: "Corrected output: " + modelData.correctedTranscript; color: Theme.ink; wrapMode: Text.Wrap; font.pixelSize: 13; Layout.fillWidth: true }
                                Label { visible: Boolean(modelData.correctionStatus); text: "Correction " + modelData.correctionStatus; color: modelData.correctionStatus === "Applied" ? Theme.success : Theme.inkMuted; font.pixelSize: 12 }
                                Label { visible: Number(modelData.wordErrorRate) >= 0; text: Number(modelData.wordErrorRate) >= 0 ? "Raw accuracy " + Math.round(Number(modelData.accuracy) * 100) + "%  WER " + Math.round(Number(modelData.wordErrorRate) * 100) + "%" : ""; color: Theme.inkMuted; font.family: Theme.monoFont; font.pixelSize: 12 }
                                Label { visible: Number(modelData.correctedWordErrorRate) >= 0; text: Number(modelData.correctedWordErrorRate) >= 0 ? "Corrected accuracy " + Math.round(Number(modelData.correctedAccuracy) * 100) + "%  WER " + Math.round(Number(modelData.correctedWordErrorRate) * 100) + "%" : ""; color: Theme.success; font.family: Theme.monoFont; font.pixelSize: 12 }
                                Label { visible: Boolean(modelData.correctionError); text: "Correction: " + modelData.correctionError; color: Theme.warning; wrapMode: Text.Wrap; font.pixelSize: 12; Layout.fillWidth: true }
                            }
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                AppButton { text: "Cancel"; visible: tests.busy; enabled: tests.busy; onClicked: tests.cancelTests() }
                ModelActionButton {
                    text: tests.busy ? "Testing..." : "Test custom set"
                    actionKind: "test"
                    enabled: root.draftSetReady && root.providerReady && !voice.modelTestBusy && !tests.busy && !tests.recording
                    hint: enabled ? "Transcribe every populated custom recording" : "Populate each recording with an expected transcription and ensure the model is ready"
                    onClicked: { root.commitAllDrafts(); tests.runTests() }
                }
            }
            Label { visible: Boolean(tests.status) || tests.busy; Layout.fillWidth: true; text: tests.busy ? (tests.status || "Testing custom recordings...") : (tests.status || ""); color: Theme.inkMuted; wrapMode: Text.Wrap; font.pixelSize: 14 }
            Label { visible: Boolean(tests.error); Layout.fillWidth: true; text: tests.error || ""; color: Theme.error; wrapMode: Text.Wrap; font.pixelSize: 14 }
        }
    }
}
