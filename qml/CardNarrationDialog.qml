import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    required property var ui
    required property var narration
    property string requestedCardId: ""
    property bool discardPromptOpen: false
    readonly property bool capturePending: narration.waitingForCapture
    readonly property bool captureInProgress: narration.recording || capturePending

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(900, ui.width - 2 * ui.gutter)
    height: Math.min(700, ui.height - 2 * ui.gutter)
    padding: 24
    modal: true
    closePolicy: Popup.NoAutoClose
    background: Rectangle { radius: 16; color: Theme.surface; border.color: Theme.ledgerRule }
    Overlay.modal: Rectangle { color: Theme.scrim }

    function openFor(id) {
        requestedCardId = String(id || "")
        if (!requestedCardId)
            return
        if (!narration.openFor(requestedCardId)) {
            ui.showError("NARRATION_EDITOR_UNAVAILABLE", narration.error || "Finish the current recording or speech test before opening card narration.", "")
            return
        }
        open()
    }
    function requestClose() {
        if (narration.busy)
            return
        if (captureInProgress || narration.hasDraft) {
            discardPrompt.open()
            return
        }
        if (narration.close() !== false)
            close()
    }
    onClosed: {
        if (narration.sessionActive && !narration.busy)
            narration.close()
    }

    header: Item {
        implicitHeight: 76
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: dialog.padding
            anchors.rightMargin: dialog.padding
            anchors.topMargin: 18
            anchors.bottomMargin: 12
            spacing: 12
            Label {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: "Record card narration"
                color: Theme.ink
                font.family: Theme.uiFont
                font.pixelSize: 22
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Label {
                visible: dialog.captureInProgress
                Layout.minimumWidth: 0
                Layout.maximumWidth: dialog.width * 0.36
                text: dialog.capturePending ? "Waiting for microphone" : narration.recordingPaused ? "Paused" : "Microphone active"
                color: dialog.capturePending || narration.recordingPaused ? Theme.warning : Theme.success
                font.family: Theme.monoFont
                font.pixelSize: 13
                elide: Text.ElideRight
            }
            GlyphButton { glyph: "close"; hint: "Close narration editor"; enabled: !narration.busy; onClicked: dialog.requestClose() }
        }
    }

    contentItem: ColumnLayout {
        spacing: 16
        RowLayout {
            Layout.fillWidth: true
            visible: narration.variants && narration.variants.length > 1
            Label { text: "Card form"; color: Theme.inkMuted; font.pixelSize: 14 }
            AppComboBox {
                id: variantPicker
                Layout.preferredWidth: 260
                model: narration.variants || []
                textRole: "label"
                currentIndex: {
                    const key = String(narration.variantKey || "")
                    const entries = narration.variants || []
                    return Math.max(0, entries.findIndex(entry => String(entry.key) === key))
                }
                enabled: !narration.busy && !dialog.captureInProgress && !narration.hasDraft
                onActivated: narration.variantKey = model[index].key
            }
            Item { Layout.fillWidth: true }
        }
        Label {
            Layout.fillWidth: true
            text: "Record up to 60 seconds for each side. A saved recording is used instead of automatic speech output for this card form."
            color: Theme.inkMuted
            wrapMode: Text.Wrap
            font.pixelSize: 14
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.width
                spacing: 14
                Repeater {
                    model: narration.sides || []
                    delegate: Rectangle {
                        id: narrationSide
                        required property var modelData
                        readonly property bool hasDraft: narration.hasDraft && narration.draftSide === modelData.side
                        Layout.fillWidth: true
                        implicitHeight: sideContent.implicitHeight + 28
                        color: Theme.ledgerSurface
                        border.color: modelData.side === narration.recordingSide && narration.recording ? Theme.success : Theme.rule
                        border.width: modelData.side === narration.recordingSide && narration.recording ? 2 : 1
                        radius: 10
                        ColumnLayout {
                            id: sideContent
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 10
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: modelData.label || modelData.side; color: Theme.ink; font.pixelSize: 17; font.bold: true }
                                Item { Layout.fillWidth: true }
                                Label {
                                    visible: modelData.hasRecording || narrationSide.hasDraft
                                    text: narrationSide.hasDraft ? "Unsaved recording" : "Recorded"
                                    color: narrationSide.hasDraft ? Theme.warning : Theme.success
                                    font.family: Theme.monoFont
                                    font.pixelSize: 12
                                }
                                Label {
                                    visible: narration.recording && narration.recordingSide === modelData.side
                                    text: String(Math.floor(narration.recordingDuration || 0)) + " s"
                                    color: Theme.ink
                                    font.family: Theme.monoFont
                                    font.pixelSize: 13
                                }
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: Math.max(62, preview.implicitHeight + 20)
                                color: Theme.editorField
                                border.color: Theme.rule
                                radius: 7
                                MarkdownPane {
                                    id: preview
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    markdown: modelData.markdown || "_No card text available._"
                                    mediaRoot: media.rootPath
                                    baseFontSize: 14
                                }
                            }
                            ProgressBar {
                                visible: narration.recording && narration.recordingSide === modelData.side
                                Layout.fillWidth: true
                                from: 0
                                to: 1
                                value: Math.min(1, Number(narration.recordingLevel || 0))
                            }
                            Flow {
                                Layout.fillWidth: true
                                spacing: 8
                                AppButton {
                                    text: narration.recording && narration.recordingSide === modelData.side
                                        ? (narration.recordingPaused ? "Resume" : "Pause") : modelData.hasRecording ? "Record again" : "Record"
                                    glyph: narration.recording && narration.recordingSide === modelData.side
                                        ? (narration.recordingPaused ? "play" : "pause") : "mic"
                                    enabled: !narration.busy && !narration.hasDraft && !dialog.capturePending && (!narration.recording || narration.recordingSide === modelData.side)
                                    onClicked: {
                                        if (narration.recording && narration.recordingSide === modelData.side) {
                                            if (narration.recordingPaused) narration.resumeRecording()
                                            else narration.pauseRecording()
                                        } else {
                                            narration.startRecording(modelData.side)
                                        }
                                    }
                                }
                                AppButton {
                                    visible: narration.recording && narration.recordingSide === modelData.side
                                    text: "Stop recording"
                                    glyph: "stop"
                                    onClicked: narration.finishRecording()
                                }
                                AppButton {
                                    visible: (modelData.hasRecording || narration.hasDraft && narration.draftSide === modelData.side) && !dialog.captureInProgress
                                    text: "Play"
                                    glyph: "play"
                                    enabled: !narration.busy
                                    onClicked: narration.play(modelData.side)
                                }
                                AppButton {
                                    text: "Discard recording"
                                    visible: narrationSide.hasDraft
                                    enabled: !narration.busy
                                    onClicked: narration.discardDraft()
                                }
                                AppButton {
                                    text: "Save recording"
                                    primary: true
                                    visible: narrationSide.hasDraft
                                    enabled: !narration.busy
                                    onClicked: narration.saveDraft()
                                }
                                AppButton {
                                    visible: narration.previewPlaying && narration.playingSide === modelData.side
                                    text: "Stop"
                                    glyph: "stop"
                                    enabled: !narration.busy
                                    onClicked: narration.stopPlayback()
                                }
                                AppButton {
                                    visible: modelData.hasRecording && !dialog.captureInProgress && !narrationSide.hasDraft
                                    text: "Remove"
                                    enabled: !narration.busy && !narration.hasDraft
                                    onClicked: narration.remove(modelData.side)
                                }
                            }
                        }
                    }
                }
            }
        }
        Label {
            visible: narration.status || narration.error
            Layout.fillWidth: true
            text: narration.error || narration.status
            color: narration.error ? Theme.error : Theme.inkMuted
            wrapMode: Text.Wrap
        }
    }

    Dialog {
        id: discardPrompt
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(480, dialog.ui.width - 2 * dialog.ui.gutter)
        padding: 20
        modal: true
        title: "Discard narration?"
        background: Rectangle { radius: 16; color: Theme.surface; border.color: Theme.ledgerRule }
        contentItem: Label {
            padding: 20
            text: "The current recording has not been saved. Discard it and close the editor?"
            color: Theme.ink
            wrapMode: Text.Wrap
        }
        footer: RowLayout {
            Item { Layout.fillWidth: true }
            AppButton { text: "Keep editing"; onClicked: discardPrompt.close() }
            AppButton {
                text: "Discard and close"
                primary: true
                onClicked: {
                    if (!narration.busy) {
                        narration.discardDraft()
                        if (narration.close() !== false) {
                            discardPrompt.close()
                            dialog.close()
                        }
                    }
                }
            }
        }
    }
}
