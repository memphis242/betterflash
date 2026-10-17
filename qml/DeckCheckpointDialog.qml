pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BetterFlash.Native 1.0 as Native

Dialog {
    id: dialog
    objectName: "deckCheckpointDialog"
    required property var ui
    property string deckId: ""
    property string deckName: ""
    property bool restoreMode: false
    property bool loading: false
    property bool submitting: false
    property bool confirming: false
    property bool deleting: false
    property string error: ""
    property string requestId: ""
    property string previewRequestId: ""
    property string operationRequestId: ""
    property int serial: 0
    property var checkpoints: []
    property var preview: ({})
    property string selectedId: ""
    property var noteDrafts: ({})
    readonly property var selected: checkpoints.find(c => c.id === selectedId) || ({})
    readonly property bool ready: !loading && !submitting && !!preview.token && !error
    readonly property bool hasChanges: Number(preview.added || 0) + Number(preview.modified || 0)
        + Number(preview.deleted || 0) + Number(preview.deckChangeCount || 0) > 0
    readonly property var historic: preview.checkpoint ? preview.checkpoint.historicalChanges || ({}) : ({})
    readonly property bool narrow: width < 600
    readonly property string footerStatus: submitting ? "Saving to persistent storage"
        : restoreMode && app.reviewing ? "End the current review before restoring."
        : restoreMode && preview.token && !hasChanges ? "This device already matches the selected checkpoint. Restoring will also reassert it when syncing."
        : !restoreMode && note.text.length > 4000 ? "Keep the note within 4,000 characters." : ""

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(1080, ui.width - 2 * ui.gutter)
    height: Math.min(820, ui.height - 36)
    padding: narrow ? 16 : 22
    modal: true
    closePolicy: submitting ? Popup.NoAutoClose : Popup.CloseOnEscape
    background: Rectangle { color: Theme.surface; radius: 18; border.color: Theme.ledgerRule }
    Overlay.modal: Rectangle { color: Theme.scrim }

    function nextRequest(kind) { return "checkpoint-" + kind + "-" + Date.now() + "-" + (++serial) }
    function counts(value) {
        return Number(value.added || 0) + " added  /  " + Number(value.modified || 0) + " modified  /  " + Number(value.deleted || 0) + " deleted"
    }
    function stamp(value) {
        const date = new Date(value)
        if (isNaN(date.getTime())) return String(value || "")
        return Qt.formatDateTime(date, "yyyy-MM-dd HH:mm")
    }
    function brief(value) {
        return (value.first ? "First checkpoint - " : "") + counts(value)
            + (Number(value.deckChangeCount || 0) ? "\n" + value.deckChangeCount + " deck changes" : "")
    }
    function openFor(id, restoring) {
        if (submitting) return
        deckId = id
        deckName = ui.deckById(id).name || "Deck"
        restoreMode = restoring
        checkpoints = []
        selectedId = ""
        preview = ({})
        error = ""
        confirming = false
        deleting = false
        note.text = noteDrafts[id] || ""
        checkpointSummary.clear()
        detailsTabs.currentIndex = 0
        open()
        refresh()
    }
    function refresh() {
        if (submitting) return
        loading = true
        confirming = false
        deleting = false
        preview = ({})
        error = ""
        previewRequestId = ""
        checkpointSummary.clear()
        requestId = nextRequest("list")
        if (!app.loadDeckCheckpoints(deckId, requestId)) {
            loading = false
            error = "CHECKPOINT_LOAD: Could not load checkpoints. Try again."
        }
    }
    function choose(checkpointId) {
        if (submitting || !checkpointId) return
        selectedId = checkpointId
        confirming = false
        deleting = false
        preview = ({})
        error = ""
        loading = true
        detailsTabs.currentIndex = 0
        previewRequestId = nextRequest("preview")
        if (!app.loadCheckpointChanges(deckId, checkpointId, previewRequestId)) {
            loading = false
            error = "CHECKPOINT_LOAD: Could not load this checkpoint. Try again."
        }
    }
    function submit() {
        if (!ready || app.busy || checkpointSummary.busy) return
        if (!restoreMode && note.text.length > 4000) {
            error = "CHECKPOINT_NOTE_LENGTH: Keep the note within 4,000 characters."
            return
        }
        if (restoreMode && !confirming) { confirming = true; return }
        operationRequestId = nextRequest(deleting ? "delete" : restoreMode ? "restore" : "create")
        submitting = true
        const queued = deleting ? app.deleteDeckCheckpoint(deckId, selectedId, operationRequestId) : restoreMode
            ? app.restoreDeckCheckpoint(deckId, selectedId, preview.token, operationRequestId)
            : app.createDeckCheckpoint(deckId, note.text, checkpointSummary.summary, preview.token, operationRequestId)
        if (!queued) {
            submitting = false
            error = restoreMode && app.reviewing ? "End the current review before restoring a checkpoint." : "Could not start this action. Refresh and try again."
        }
    }
    onClosed: {
        const drafts = Object.assign({}, noteDrafts)
        drafts[deckId] = note.text
        noteDrafts = drafts
        requestId = ""
        previewRequestId = ""
        checkpointSummary.clear()
    }
    Connections {
        target: app
        function onDeckCheckpointsReady(deckId, result, requestId) {
            if (!dialog.visible || deckId !== dialog.deckId || requestId !== dialog.requestId) return
            dialog.loading = false
            dialog.checkpoints = result.checkpoints || []
            if (!result.ok) { dialog.error = result.error || "Could not read checkpoints."; return }
            if (dialog.restoreMode) {
                if (!dialog.checkpoints.length) return
                const oldSelection = dialog.checkpoints.find(c => c.id === dialog.selectedId)
                dialog.choose(oldSelection ? dialog.selectedId : dialog.checkpoints[dialog.checkpoints.length - 1].id)
                Qt.callLater(function() {
                    timeline.currentIndex = Math.max(0, dialog.checkpoints.findIndex(c => c.id === dialog.selectedId))
                    timeline.positionViewAtIndex(timeline.currentIndex, ListView.Contain)
                })
            } else dialog.preview = result.preview || ({})
        }
        function onCheckpointChangesReady(deckId, checkpointId, result, requestId) {
            if (!dialog.visible || deckId !== dialog.deckId || checkpointId !== dialog.selectedId || requestId !== dialog.previewRequestId) return
            dialog.loading = false
            if (!result.ok) { dialog.error = result.error || "Could not read checkpoint changes."; return }
            dialog.preview = result
        }
        function onDeckCheckpointFinished(deckId, checkpointId, action, success, requestId) {
            if (!dialog.visible || deckId !== dialog.deckId || requestId !== dialog.operationRequestId) return
            dialog.submitting = false
            if (!success) {
                dialog.confirming = false
                dialog.error = "The action was not completed. Refresh the change list and try again. Your checkpoint note has been kept."
                return
            }
            if (action === "restore" && app.lastError && app.lastError.code) {
                dialog.confirming = false
                dialog.error = "The checkpoint was restored, but the display could not be refreshed. Refresh to reload the saved state."
                return
            }
            if (action === "delete") {
                dialog.selectedId = ""
                dialog.refresh()
                return
            }
            if (action === "create") note.text = ""
            dialog.close()
        }
    }
    header: Item {
        implicitHeight: 88
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: dialog.narrow ? 16 : 24; anchors.rightMargin: dialog.narrow ? 16 : 24
            anchors.topMargin: 18; anchors.bottomMargin: 12
            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 4
                Label {
                    text: dialog.restoreMode ? "Restore checkpoint" : "Create checkpoint"
                    color: Theme.ink; font.pixelSize: dialog.narrow ? 19 : 23; font.bold: true
                    Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight
                    ToolTip.visible: titleHover.hovered && truncated
                    ToolTip.text: text
                    HoverHandler { id: titleHover }
                }
                Label {
                    text: dialog.deckName; textFormat: Text.PlainText; color: Theme.inkMuted
                    font.pixelSize: 14; Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight
                    ToolTip.visible: deckTitleHover.hovered && truncated
                    ToolTip.text: text
                    HoverHandler { id: deckTitleHover }
                }
            }
            GlyphButton { glyph: "refresh"; hint: "Refresh checkpoints and changes"; enabled: !dialog.loading && !dialog.submitting && !checkpointSummary.busy; onClicked: { if (dialog.restoreMode) app.refresh(); dialog.refresh() } }
            GlyphButton { glyph: "close"; hint: "Close checkpoint dialog"; enabled: !dialog.submitting; onClicked: dialog.close() }
        }
    }
    contentItem: ScrollView {
        id: bodyScroll
        clip: true
        contentWidth: availableWidth
        contentHeight: bodyLayout.implicitHeight
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ColumnLayout {
            id: bodyLayout
            width: bodyScroll.availableWidth
            spacing: 12
            readonly property real chromeHeight: {
                const sections = [introduction, timelineSection, emptyState, changeHeading, detailsTabs,
                                  annotationSection, loadingLabel, checkpointError, confirmationPanel]
                let total = 0
                let count = changePreviewSection.visible ? 1 : 0
                for (const section of sections) {
                    if (!section.visible) continue
                    total += section.implicitHeight
                    ++count
                }
                return total + Math.max(0, count - 1) * spacing
            }
            Label {
                id: introduction
                Layout.fillWidth: true
                text: dialog.restoreMode ? "Restore deck content and child decks. Existing review history and schedules are preserved. Sync recreates missing parent folders if needed."
                    : "Save this deck, its child decks, and their cards, images, and recordings."
                color: Theme.inkMuted; font.pixelSize: 13; wrapMode: Text.Wrap
            }
            Rectangle {
                id: timelineSection
                visible: dialog.restoreMode && dialog.checkpoints.length > 0
                Layout.fillWidth: true
                implicitHeight: 224
                Layout.minimumHeight: implicitHeight
                Layout.preferredHeight: implicitHeight
                Layout.maximumHeight: implicitHeight
                radius: 8
                color: Theme.editorField
                border.color: Theme.ledgerRule
                ListView {
                    id: timeline
                    objectName: "checkpointTimeline"
                    anchors.fill: parent
                    anchors.margins: 12
                    clip: true
                    model: dialog.checkpoints
                    orientation: ListView.Horizontal
                    spacing: 0
                    boundsBehavior: Flickable.StopAtBounds
                    enabled: !dialog.submitting
                    ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AlwaysOn }
                    delegate: Item {
                        id: checkpointNode
                        required property var modelData
                        required property int index
                        width: 260
                        height: timeline.height - 16
                        readonly property bool selected: modelData.id === dialog.selectedId
                        Rectangle { x: 0; y: 18; width: parent.width; height: 1; color: Theme.ledgerRule }
                        MouseArea {
                            anchors.fill: parent
                            enabled: !dialog.submitting
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { timeline.currentIndex = checkpointNode.index; dialog.choose(checkpointNode.modelData.id) }
                        }
                        Button {
                            id: nodeButton
                            x: 12; y: 4
                            width: 28; height: 28
                            onClicked: { timeline.currentIndex = checkpointNode.index; dialog.choose(checkpointNode.modelData.id) }
                            Accessible.name: "Checkpoint " + (checkpointNode.index + 1) + ", " + dialog.stamp(checkpointNode.modelData.createdAt)
                            ToolTip.visible: hovered
                            ToolTip.text: Accessible.name
                            background: Rectangle {
                                radius: width / 2
                                color: nodeButton.hovered ? Theme.accentSoft : Theme.editorField
                                border.color: checkpointNode.selected ? Theme.accent : Theme.inkMuted
                                border.width: checkpointNode.selected ? 3 : 1
                                Rectangle { anchors.centerIn: parent; width: 8; height: 8; radius: 4; color: Theme.accent; visible: checkpointNode.selected }
                            }
                            contentItem: Item {}
                        }
                        Column {
                            x: 12; y: 42; width: parent.width - 28
                            spacing: 5
                            Label { text: dialog.stamp(checkpointNode.modelData.createdAt); color: checkpointNode.selected ? Theme.accent : Theme.ink; font.pixelSize: 13; font.bold: checkpointNode.selected }
                            Label { text: dialog.brief(checkpointNode.modelData); color: Theme.inkMuted; font.pixelSize: 11; width: parent.width; wrapMode: Text.Wrap }
                            Label {
                                visible: !checkpointNode.selected
                                text: checkpointNode.modelData.summary || checkpointNode.modelData.note || ""
                                textFormat: Text.PlainText
                                color: Theme.inkMuted; font.pixelSize: 12
                                width: parent.width; wrapMode: Text.Wrap
                                maximumLineCount: 3; elide: Text.ElideRight
                            }
                            Rectangle {
                                visible: checkpointNode.selected
                                width: parent.width
                                height: 76
                                radius: 5
                                color: Theme.surfaceRaised
                                border.color: Theme.accent
                                Label {
                                    anchors.fill: parent; anchors.margins: 9
                                    text: checkpointNode.modelData.note || checkpointNode.modelData.summary || "No annotation"
                                    textFormat: Text.PlainText
                                    color: Theme.ink; font.pixelSize: 12
                                    wrapMode: Text.Wrap; elide: Text.ElideRight
                                    maximumLineCount: 3
                                }
                            }
                        }
                    }
                    Keys.onLeftPressed: if (count > 0) { currentIndex = Math.max(0, currentIndex - 1); dialog.choose(dialog.checkpoints[currentIndex].id) }
                    Keys.onRightPressed: if (count > 0) { currentIndex = Math.min(count - 1, currentIndex + 1); dialog.choose(dialog.checkpoints[currentIndex].id) }
                }
            }
            Label {
                id: emptyState
                visible: dialog.restoreMode && !dialog.loading && !dialog.checkpoints.length && !dialog.error
                Layout.fillWidth: true
                text: "No checkpoints yet. Create a checkpoint from this deck's menu to save its current content."
                color: Theme.inkMuted; wrapMode: Text.Wrap
                verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignHCenter
            }
            GridLayout {
                id: changeHeading
                columns: dialog.narrow ? 1 : 2
                visible: !!dialog.preview.token
                Layout.fillWidth: true
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3
                    Label {
                        text: dialog.restoreMode ? "Changes this restore will make" : dialog.preview.first ? "First checkpoint - a new starting point" : "Changes since the last checkpoint"
                        color: Theme.ink; font.pixelSize: 17; font.bold: true
                        Layout.fillWidth: true; wrapMode: Text.Wrap
                    }
                    Label { text: dialog.counts(dialog.preview); color: Theme.inkMuted; font.pixelSize: 13; wrapMode: Text.Wrap; Layout.fillWidth: true }
                }
                Label {
                    visible: !dialog.restoreMode && !!dialog.preview.first
                    text: (dialog.preview.cardCount || 0) + " cards / " + (dialog.preview.deckCount || 0) + " decks"
                    color: Theme.inkMuted; font.pixelSize: 12
                    Layout.fillWidth: dialog.narrow
                }
            }
            TabBar {
                id: detailsTabs
                visible: !!dialog.preview.token
                Layout.fillWidth: true
                AppTabButton {
                    text: dialog.narrow ? "Cards" : "Cards (" + (dialog.preview.changes || []).length + ")"
                    width: dialog.narrow ? (detailsTabs.width - detailsTabs.spacing * (dialog.restoreMode ? 2 : 1)) / (dialog.restoreMode ? 3 : 2) : implicitWidth + 20
                    font.pixelSize: dialog.narrow ? 15 : 17
                    implicitHeight: dialog.narrow ? 44 : 48
                    ToolTip.visible: hovered && dialog.narrow
                    ToolTip.text: "Card changes: " + (dialog.preview.changes || []).length
                }
                AppTabButton {
                    text: dialog.narrow ? "Decks" : "Decks (" + (dialog.preview.deckChanges || []).length + ")"
                    width: dialog.narrow ? (detailsTabs.width - detailsTabs.spacing * (dialog.restoreMode ? 2 : 1)) / (dialog.restoreMode ? 3 : 2) : implicitWidth + 20
                    font.pixelSize: dialog.narrow ? 15 : 17
                    implicitHeight: dialog.narrow ? 44 : 48
                    ToolTip.visible: hovered && dialog.narrow
                    ToolTip.text: "Deck changes: " + (dialog.preview.deckChanges || []).length
                }
                AppTabButton {
                    visible: dialog.restoreMode
                    text: dialog.narrow ? "Details" : "Checkpoint details"
                    width: visible ? dialog.narrow ? (detailsTabs.width - detailsTabs.spacing * 2) / 3 : implicitWidth + 20 : 0
                    font.pixelSize: dialog.narrow ? 15 : 17
                    implicitHeight: dialog.narrow ? 44 : 48
                    ToolTip.visible: hovered && dialog.narrow
                    ToolTip.text: "Checkpoint details"
                }
            }
            Rectangle {
                id: changePreviewSection
                visible: !!dialog.preview.token
                Layout.fillWidth: true
                Layout.minimumHeight: Layout.preferredHeight
                Layout.preferredHeight: Math.max(190, bodyScroll.availableHeight - bodyLayout.chromeHeight)
                Layout.maximumHeight: Layout.preferredHeight
                radius: 9
                color: Theme.editorField
                border.color: Theme.ledgerRule
                StackLayout {
                    id: changePreview
                    anchors.fill: parent
                    anchors.margins: dialog.narrow ? 10 : 16
                    currentIndex: detailsTabs.currentIndex
                    Item {
                        CheckpointChangeList { anchors.fill: parent; changes: dialog.preview.changes || [] }
                        Label { visible: !(dialog.preview.changes || []).length; anchors.centerIn: parent; text: "No card changes"; color: Theme.inkMuted }
                    }
                    Item {
                        CheckpointChangeList { anchors.fill: parent; changes: dialog.preview.deckChanges || []; deckChanges: true }
                        Label { visible: !(dialog.preview.deckChanges || []).length; anchors.centerIn: parent; text: "No deck changes"; color: Theme.inkMuted }
                    }
                    ScrollView {
                        id: checkpointDetails
                        clip: true; contentWidth: availableWidth
                        ColumnLayout {
                            width: checkpointDetails.availableWidth
                            spacing: 12
                            GridLayout {
                                columns: dialog.narrow ? 1 : 2
                                Layout.fillWidth: true
                                Label { text: "Saved " + dialog.stamp(dialog.selected.createdAt); color: Theme.ink; font.pixelSize: 15; Layout.fillWidth: true; wrapMode: Text.Wrap }
                                AppButton {
                                    text: "Delete checkpoint"
                                    foreground: Theme.error
                                    Layout.alignment: Qt.AlignRight
                                    enabled: !dialog.submitting
                                    onClicked: { dialog.deleting = true; dialog.confirming = true }
                                }
                            }
                            Label { text: dialog.brief(dialog.historic.first !== undefined ? dialog.historic : dialog.selected); color: Theme.inkMuted; wrapMode: Text.Wrap; Layout.fillWidth: true }
                            Label { visible: !!dialog.selected.note; text: "Note"; font.bold: true; color: Theme.ink }
                            TextEdit { visible: !!dialog.selected.note; text: dialog.selected.note || ""; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; color: Theme.ink; font.pixelSize: 14; Layout.fillWidth: true }
                            Label { visible: !!dialog.selected.summary; text: "Summary of changes"; font.bold: true; color: Theme.ink }
                            Native.MarkdownView { visible: !!dialog.selected.summary; markdown: dialog.selected.summary || ""; foreground: Theme.ink; darkMode: Theme.dark; codeBackground: Theme.editorField; accent: Theme.accent; baseFontSize: 14; mediaRoot: app.mediaPath; Layout.fillWidth: true; Layout.preferredHeight: implicitHeight }
                        }
                    }
                }
            }
            ColumnLayout {
                id: annotationSection
                visible: !dialog.restoreMode && !!dialog.preview.token
                Layout.fillWidth: true
                spacing: 7
                Label { text: "Checkpoint note (optional)"; color: Theme.ink; font.pixelSize: 14 }
                ScrollView {
                    id: noteScroll
                    Layout.fillWidth: true; Layout.preferredHeight: 76
                    contentWidth: availableWidth
                    clip: true
                    background: Rectangle { color: Theme.editorField; border.color: note.activeFocus ? Theme.accent : Theme.rule; border.width: note.activeFocus ? 2 : 1; radius: 6 }
                    TextArea { id: note; width: noteScroll.availableWidth; wrapMode: TextEdit.Wrap; selectByMouse: true; color: Theme.ink; font.pixelSize: 14; padding: 10; background: Item {}
                        enabled: !dialog.submitting; placeholderText: "What would you like to remember about this version?"
                        onTextChanged: if (text.length <= 4000 && dialog.error.indexOf("CHECKPOINT_NOTE_LENGTH:") === 0) dialog.error = ""
                        Keys.onPressed: function(event) {
                            if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && event.modifiers & Qt.ControlModifier) {
                                dialog.submit()
                                event.accepted = true
                            }
                        }
                    }
                }
                GridLayout {
                    columns: dialog.narrow ? 1 : 2
                    Layout.fillWidth: true
                    AppButton {
                        text: checkpointSummary.busy ? "Cancel generation" : "Generate LLM summary of changes"
                        hint: checkpointSummary.busy ? "Cancel summary generation" : "Generate a two or three sentence annotation of all checkpoint changes"
                        Layout.minimumWidth: 0
                        Layout.preferredWidth: Math.min(implicitWidth, bodyScroll.availableWidth)
                        Layout.fillWidth: dialog.narrow
                        enabled: !dialog.loading && !dialog.submitting
                        onClicked: checkpointSummary.busy ? checkpointSummary.cancel() : checkpointSummary.generate(dialog.preview, dialog.deckName)
                    }
                    Label { text: ai.provider === "local" ? "Using " + ai.localModelLabel + " on this device" : "Changes will be sent to " + ai.endpoint; color: Theme.inkMuted; font.pixelSize: 11; wrapMode: Text.Wrap; Layout.fillWidth: true }
                }
                Label {
                    visible: checkpointSummary.busy
                    text: checkpointSummary.status
                    color: Theme.inkMuted
                    font.pixelSize: 12
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                }
                ScrollView {
                    visible: checkpointSummary.summary.length > 0
                    Layout.fillWidth: true; Layout.preferredHeight: 66
                    contentWidth: availableWidth; clip: true
                    TextEdit { width: parent.width; text: checkpointSummary.summary; textFormat: TextEdit.PlainText; color: Theme.ink; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; font.pixelSize: 13 }
                }
            }
            Label { id: loadingLabel; visible: dialog.loading; text: "Loading checkpoint changes"; color: Theme.inkMuted; Layout.fillWidth: true; wrapMode: Text.Wrap; verticalAlignment: Text.AlignVCenter }
            ErrorBlock { id: checkpointError; visible: !!dialog.error || !!checkpointSummary.error; message: dialog.error || checkpointSummary.error; Layout.fillWidth: true }
            Rectangle {
                id: confirmationPanel
                visible: dialog.confirming
                Layout.fillWidth: true
                implicitHeight: confirmation.implicitHeight + 22
                radius: 6; color: Theme.editorField; border.color: Theme.warning
                Label {
                    id: confirmation
                    x: 11; y: 11; width: parent.width - 22
                    text: dialog.deleting ? "Delete the checkpoint saved on " + dialog.stamp(dialog.selected.createdAt) + " and its annotation? Current decks, cards, and review history are preserved. This checkpoint cannot be recovered after deletion."
                        : "Restore the content saved on " + dialog.stamp(dialog.selected.createdAt) + "? Cards added since then will be removed; changed and deleted cards will return to the saved version. A recovery checkpoint of the current state is saved first."
                    color: Theme.ink; font.pixelSize: 13; wrapMode: Text.Wrap
                }
            }
        }
    }
    footer: Item {
        implicitHeight: footerLayout.implicitHeight + 24
        ColumnLayout {
            id: footerLayout
            anchors.fill: parent
            anchors.leftMargin: dialog.narrow ? 16 : 24
            anchors.rightMargin: dialog.narrow ? 16 : 24
            anchors.topMargin: 10
            anchors.bottomMargin: 14
            spacing: 8
            Label {
                visible: dialog.narrow && !!text
                text: dialog.footerStatus
                color: Theme.inkMuted; wrapMode: Text.Wrap; font.pixelSize: 12
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Label {
                    visible: !dialog.narrow
                    text: dialog.footerStatus
                    color: Theme.inkMuted; wrapMode: Text.Wrap; font.pixelSize: 12
                    Layout.fillWidth: true
                }
                Item { visible: dialog.narrow; Layout.fillWidth: true }
                AppButton { text: dialog.confirming ? "Back" : "Cancel"; enabled: !dialog.submitting; onClicked: { if (dialog.confirming) { dialog.confirming = false; dialog.deleting = false } else dialog.close() } }
                AppButton {
                    text: dialog.submitting ? "Saving" : dialog.deleting ? "Delete checkpoint" : dialog.restoreMode ? dialog.confirming ? "Restore checkpoint" : "Restore selected" : "Create checkpoint"
                    primary: true
                    enabled: dialog.ready && !app.busy && !checkpointSummary.busy && (!dialog.restoreMode || !!dialog.selectedId && (dialog.deleting || !app.reviewing)) && (dialog.restoreMode || note.text.length <= 4000)
                    onClicked: dialog.submit()
                }
            }
        }
    }
}
