pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "atomicDialog"
    required property var ui
    property var drafts: []
    readonly property bool applying: atomic.applying
    property bool prepared: false
    property bool seeded: false
    property bool editingSet: false
    property bool loading: false
    property bool preview: false
    property string localError: ""
    property string generationInstructions: ""
    readonly property var failure: localError.length > 0 ? ({
        code: "ATOMIC_DRAFT",
        message: "A proposed card is missing required content.",
        action: "Write a front for every card and a back for cards without cloze markers, then choose Replace again.",
        detail: localError
    }) : atomic.errorInfo
    readonly property bool hasFailure: !!failure.message
    onFailureChanged: {
        if (atomicError)
            atomicError.expanded = false
        if (visible && hasFailure)
            Qt.callLater(function() { if (dialog.visible && dialog.hasFailure) errorPanel.forceActiveFocus() })
    }
    readonly property int draftIndex: atomicTabs.currentIndex - 1
    readonly property var currentDraft: draftIndex >= 0 && draftIndex < drafts.length ? drafts[draftIndex] : ({})
    readonly property bool splitReady: editingSet && drafts.length >= 2 && drafts.length <= atomic.maxCards

    title: "Atomicize card"
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    width: Math.min(900, ui.width - 2 * ui.gutter)
    height: Math.min(760, ui.height - 36)
    padding: ui.width < 600 ? 14 : 20
    focus: true
    closePolicy: Popup.NoAutoClose
    header: Item {
        implicitHeight: 64
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: dialog.padding
            anchors.rightMargin: dialog.padding
            spacing: 12
            Label {
                text: dialog.title
                color: Theme.ink
                font.pixelSize: 22
                font.bold: true
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            GlyphButton {
                glyph: "close"
                hint: "Close Atomicize"
                enabled: !atomic.busy
                opacity: atomic.busy ? 0.45 : 1
                onClicked: dialog.close()
            }
        }
    }
    background: Rectangle {
        color: Theme.surface
        border.color: Theme.rule
        radius: 6
    }
    Overlay.modal: Rectangle {
        color: Theme.scrim
    }

    function openFor(cardId) {
        if (applying)
            return
        atomic.clear()
        generationInstructions = ""
        drafts = []
        seeded = false
        editingSet = false
        preview = false
        localError = ""
        atomicTabs.currentIndex = 0
        open()
        prepared = atomic.prepare(cardId)
    }
    function generate() {
        if (atomic.busy || app.busy || !prepared || !atomic.sourceCard.id)
            return
        const cardId = atomic.sourceCard.id
        drafts = []
        seeded = false
        editingSet = false
        preview = false
        localError = ""
        atomicTabs.currentIndex = 0
        atomic.propose(cardId, generationInstructions)
    }
    function startManual() {
        if (atomic.busy || app.busy || !prepared || !atomic.beginManual())
            return
        seeded = true
        editingSet = true
        preview = false
        localError = ""
        drafts = [{ front: "", back: "", pointCount: 1 }, { front: "", back: "", pointCount: 1 }]
        atomicTabs.currentIndex = 1
        loadDraft()
        Qt.callLater(function() { draftSource.forceActiveFocus() })
    }
    function addDraft() {
        if (!editingSet || atomic.busy || app.busy || drafts.length >= atomic.maxCards)
            return
        updateDraft()
        drafts = drafts.concat([{ front: "", back: "", pointCount: 1 }])
        preview = false
        atomicTabs.currentIndex = drafts.length
        loadDraft()
        Qt.callLater(function() { draftSource.forceActiveFocus() })
    }
    function removeDraft() {
        if (!editingSet || atomic.busy || app.busy || draftIndex < 0)
            return
        const index = draftIndex
        const next = drafts.slice()
        next.splice(index, 1)
        drafts = next
        localError = ""
        atomicTabs.currentIndex = drafts.length === 0 ? 0 : Math.min(index + 1, drafts.length)
        loadDraft()
    }
    function loadDraft() {
        if (!draftSource || !points)
            return
        loading = true
        const index = atomicTabs.currentIndex - 1
        const card = index >= 0 && index < drafts.length ? drafts[index] : ({})
        draftSource.text = card.front !== undefined ? card.front + "\n\n---\n\n" + card.back : ""
        points.value = card.pointCount || 1
        loading = false
    }
    function updateDraft() {
        const index = atomicTabs.currentIndex - 1
        if (loading || index < 0 || index >= drafts.length)
            return
        const parts = ui.splitSource(draftSource.text)
        const next = drafts.slice()
        next[index] = {
            front: parts.front,
            back: parts.back,
            pointCount: points.value
        }
        drafts = next
        localError = ""
    }
    function submit() {
        if (!splitReady || atomic.busy || app.busy)
            return
        updateDraft()
        for (let i = 0; i < drafts.length; ++i) {
            if (!drafts[i].front.trim() || (!app.hasClozeMarkers(drafts[i].front) && !drafts[i].back.trim())) {
                localError = "ATOMIC_DRAFT: Write a front for every card and a back for cards without cloze markers."
                atomicTabs.currentIndex = i + 1
                draftSource.forceActiveFocus()
                return
            }
        }
        atomic.apply(drafts)
    }
    onClosed: {
        if (!applying) {
            atomic.cancel()
            atomic.clear()
            drafts = []
            seeded = false
            editingSet = false
        }
    }
    Connections {
        target: atomic
        function onChanged() {
            if (!dialog.visible)
                return
            if (!dialog.seeded && atomic.hasProposal) {
                dialog.drafts = atomic.proposals.map(card => ({
                            front: card.front,
                            back: card.back,
                            pointCount: card.pointCount || 1
                        }))
                dialog.seeded = true
                dialog.editingSet = true
                atomicTabs.currentIndex = 1
                dialog.loadDraft()
            }
        }
        function onApplied() {
            if (!dialog.visible)
                return
            const deckId = atomic.sourceCard.deckId
            dialog.close()
            dialog.ui.selectedCardId = ""
            dialog.ui.detailOpen = false
            dialog.ui.setDeck(deckId)
        }
    }
    contentItem: ColumnLayout {
        spacing: 10
        Rectangle {
            id: errorPanel
            visible: dialog.hasFailure
            color: Theme.surfaceRaised
            border.color: Theme.error
            border.width: 2
            radius: 10
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(errorContents.implicitHeight + 24, Math.max(120, dialog.availableHeight * 0.4))
            activeFocusOnTab: true
            Accessible.role: Accessible.AlertMessage
            Accessible.name: (dialog.failure.message || "") + " " + (dialog.failure.action || "")
            ScrollView {
                id: errorScroll
                anchors.fill: parent
                anchors.margins: 12
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    id: errorContents
                    width: errorScroll.availableWidth
                    spacing: 8
                    Label {
                        text: dialog.failure.message || ""
                        textFormat: Text.PlainText
                        font.family: Theme.contentFont
                        font.pixelSize: 18
                        font.bold: true
                        color: Theme.error
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    ErrorBlock {
                        id: atomicError
                        objectName: "atomicError"
                        textSize: 17
                        message: dialog.failure.action || ""
                        details: (dialog.failure.code || "") + (dialog.failure.detail ? "\n\n" + dialog.failure.detail : "")
                        Layout.fillWidth: true
                    }
                }
            }
        }
        AppButton {
            id: generateProposal
            objectName: "atomicGenerate"
            hint: ai.provider === "local" ? "Generate proposal processes the front and back on this device using " + ai.localModelLabel + ". Related lists and procedures can stay together."
                : "Generate proposal sends the front and back to " + ai.endpoint + " using " + ai.model + ". Related lists and procedures can stay together."
            Accessible.description: hint
            readonly property bool retry: atomic.error.length > 0 && !atomic.busy
            foreground: retry ? Theme.ink : Theme.actionInk
            background: Rectangle {
                radius: 14
                color: !generateProposal.enabled ? Theme.surfaceRaised
                    : generateProposal.retry ? (generateProposal.hovered || generateProposal.down ? Theme.retryActionHover : Theme.retryActionFill)
                    : Theme.actionFill
                border.width: generateProposal.activeFocus || generateProposal.hovered ? 3 : 2
                border.color: generateProposal.retry ? Theme.retryActionOutline : Theme.accent
                opacity: generateProposal.enabled ? 1 : 0.7
            }
            visible: !dialog.editingSet
            text: atomic.busy ? "Generating proposal" : atomic.error.length > 0 ? "Try again" : atomic.decision === "keep" ? "Generate again" : "Generate Proposal"
            primary: true
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            font.pixelSize: 17
            enabled: !atomic.busy && !app.busy && dialog.prepared && !!atomic.sourceCard.id
            onClicked: dialog.generate()
        }
        AppButton {
            objectName: "atomicCancelRequest"
            fill: Theme.cancelActionFill
            hoverFill: Theme.cancelActionHover
            outline: Theme.error
            focusOutline: Theme.error
            text: "Cancel request"
            hint: "Stop generating and keep the original card unchanged"
            visible: atomic.busy && !dialog.applying
            Layout.fillWidth: true
            onClicked: atomic.cancel()
        }
        Rectangle {
            id: resultPanel
            visible: !atomic.busy && !dialog.hasFailure && (atomic.decision === "keep" || atomic.decision === "split")
            color: Theme.surfaceRaised
            border.color: atomic.decision === "keep" ? Theme.warning : Theme.rule
            radius: 8
            Layout.fillWidth: true
            Layout.preferredHeight: resultContents.implicitHeight + 28
            ColumnLayout {
                id: resultContents
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 14
                spacing: 10
                Label {
                    text: atomic.decision === "keep" ? "Recommendation: keep this card together"
                        : "Recommendation: split into " + atomic.proposals.length + " cards"
                    textFormat: Text.PlainText
                    color: atomic.decision === "keep" ? Theme.warning : Theme.ink
                    font.family: Theme.contentFont
                    font.pixelSize: 18
                    font.bold: true
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                Label {
                    visible: atomic.decision === "keep" && atomic.hasProposal
                    text: "The model also offered optional atomic replacement candidates for you to consider."
                    textFormat: Text.PlainText
                    color: Theme.ink
                    font.family: Theme.contentFont
                    font.pixelSize: 16
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                ScrollView {
                    id: reasonScroll
                    objectName: "atomicReasonScroll"
                    visible: atomic.reason.length > 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(reasonLabel.contentHeight, dialog.ui.width < 600 ? 88 : 112)
                    contentWidth: availableWidth
                    clip: true
                    TextEdit {
                        id: reasonLabel
                        width: reasonScroll.availableWidth
                        text: atomic.reason
                        textFormat: TextEdit.PlainText
                        readOnly: true
                        selectByMouse: true
                        cursorVisible: false
                        color: Theme.ink
                        font.family: Theme.contentFont
                        font.pixelSize: 16
                        wrapMode: TextEdit.Wrap
                    }
                }
            }
        }
        ColumnLayout {
            visible: atomic.busy || (!dialog.hasFailure && atomic.decision.length === 0 && atomic.status.length > 0)
            Layout.fillWidth: true
            spacing: 8
            Label {
                textFormat: Text.PlainText
                text: dialog.applying ? "Saving the replacement cards..." : atomic.status
                color: Theme.ink
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                font.pixelSize: atomic.busy ? 17 : 14
                font.bold: atomic.busy
                Accessible.role: Accessible.StaticText
            }
            ProgressBar {
                objectName: "atomicProgress"
                visible: atomic.busy
                indeterminate: atomic.busy
                Layout.fillWidth: true
                Accessible.name: dialog.applying ? "Saving replacement cards" : "Generating card proposal"
            }
        }
        GridLayout {
            visible: !dialog.editingSet
            columns: dialog.ui.width < 600 ? 1 : 3
            Layout.fillWidth: true
            AppButton {
                objectName: "atomicEditInstructions"
                Layout.fillWidth: dialog.ui.width < 600
                text: dialog.generationInstructions.length > 0 ? "Edit instructions (custom)" : "Edit instructions"
                enabled: !atomic.busy && !app.busy
                onClicked: instructionsDialog.openEditor()
            }
            AppButton {
                objectName: "atomicManual"
                Layout.fillWidth: dialog.ui.width < 600
                text: "Create split manually"
                hint: "Write replacement cards without calling the language model"
                enabled: dialog.prepared && !atomic.busy && !app.busy
                onClicked: dialog.startManual()
            }
            Item { visible: dialog.ui.width >= 600; Layout.fillWidth: true }
        }
        RowLayout {
            visible: !dialog.editingSet && ai.provider === "remote" && !ai.hasApiKey
            Layout.fillWidth: true
            Label {
                textFormat: Text.PlainText
                text: "Add a language model key in Settings to generate a proposal, or create a split manually."
                color: Theme.inkMuted
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            AppButton {
                objectName: "atomicConfigure"
                text: "Settings"
                onClicked: {
                    dialog.close()
                    dialog.ui.openLanguageModelSettings()
                }
            }
        }
        GridLayout {
            visible: dialog.editingSet
            columns: dialog.ui.width < 600 ? 2 : 3
            Layout.fillWidth: true
            Label {
                Layout.columnSpan: dialog.ui.width < 600 ? 2 : 1
                text: dialog.drafts.length + " / " + atomic.maxCards + " cards"
                color: Theme.inkMuted
                font.family: Theme.monoFont
                Layout.fillWidth: true
            }
            AppButton {
                objectName: "atomicAddCard"
                text: "Add card"
                hint: "Add a manually written card to this replacement set (up to " + atomic.maxCards + " cards)"
                enabled: !atomic.busy && !app.busy && dialog.drafts.length < atomic.maxCards
                onClicked: dialog.addDraft()
            }
            AppButton {
                objectName: "atomicRemoveCard"
                text: "Remove card"
                hint: "Remove this unsaved draft from the replacement set"
                outline: Theme.error
                enabled: !atomic.busy && !app.busy && dialog.draftIndex >= 0
                onClicked: dialog.removeDraft()
            }
        }
        Rectangle {
            visible: dialog.drafts.length === 0
            color: Theme.rule
            Layout.fillWidth: true
            implicitHeight: 1
        }
        Label {
            visible: dialog.drafts.length === 0
            text: "Source"
            color: Theme.ink
            font.family: Theme.contentFont
            font.pixelSize: 18
            font.bold: true
            Layout.fillWidth: true
        }
        TabBar {
            id: atomicTabs
            visible: dialog.drafts.length > 0
            objectName: "atomicTabs"
            Layout.fillWidth: true
            enabled: !dialog.applying
            onCurrentIndexChanged: dialog.loadDraft()
            AppTabButton {
                text: "Source"
                font.pixelSize: 12
                padding: 5
                width: Math.max(88, atomicTabs.width / (dialog.drafts.length + 1))
            }
            Repeater {
                model: dialog.drafts.length
                delegate: AppTabButton {
                    required property int index
                    text: "Card " + (index + 1)
                    font.pixelSize: 12
                    padding: 5
                    width: Math.max(88, atomicTabs.width / (dialog.drafts.length + 1))
                }
            }
        }
        RowLayout {
            visible: dialog.draftIndex >= 0
            Layout.fillWidth: true
            AppButton {
                objectName: "atomicPreviewToggle"
                text: dialog.preview ? "Edit Markdown" : "Preview"
                enabled: !dialog.applying
                onClicked: dialog.preview = !dialog.preview
            }
            Item {
                Layout.fillWidth: true
            }
            Label {
                textFormat: Text.PlainText
                id: atomicPointsLabel
                readonly property string hint: points.value + " partial answer points: the number of points within this answer that can receive credit for partial grading."
                text: "Partial answer points"
                color: Theme.inkMuted
                ToolTip.visible: pointsHover.containsMouse
                ToolTip.text: hint
                MouseArea {
                    id: pointsHover
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.NoButton
                }
            }
            SpinBox {
                id: points
                objectName: "atomicPoints"
                Accessible.name: "Partial answer points"
                Accessible.description: atomicPointsLabel.hint
                ToolTip.visible: hovered
                ToolTip.text: atomicPointsLabel.hint
                from: 1
                to: 1000
                editable: true
                enabled: !dialog.applying
                onValueChanged: dialog.updateDraft()
            }
        }
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: dialog.draftIndex < 0 || dialog.preview ? 0 : 1
            ScrollView {
                id: atomicPreview
                objectName: "atomicPreviewScroll"
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: atomicPreview.availableWidth
                    spacing: 16
                    Label {
                        textFormat: Text.PlainText
                        text: "Front"
                        color: Theme.inkMuted
                        font.family: Theme.monoFont
                        font.pixelSize: 11
                    }
                    MarkdownPane {
                        markdown: dialog.draftIndex < 0 ? (atomic.sourceCard.front || "") : (dialog.currentDraft.front || "")
                        clozePreview: app.hasClozeMarkers(markdown)
                        mediaRoot: media.rootPath
                        baseFontSize: 18
                        Layout.fillWidth: true
                        Layout.preferredHeight: implicitHeight
                    }
                    Rectangle {
                        color: Theme.rule
                        height: 1
                        Layout.fillWidth: true
                    }
                    Label {
                        textFormat: Text.PlainText
                        text: "Back"
                        color: Theme.inkMuted
                        font.family: Theme.monoFont
                        font.pixelSize: 11
                    }
                    MarkdownPane {
                        markdown: dialog.draftIndex < 0 ? (atomic.sourceCard.back || "") : (dialog.currentDraft.back || "")
                        clozePreview: app.hasClozeMarkers(markdown)
                        mediaRoot: media.rootPath
                        baseFontSize: 18
                        Layout.fillWidth: true
                        Layout.preferredHeight: implicitHeight
                    }
                }
            }
            ScrollView {
                clip: true
                contentWidth: availableWidth
                TextArea {
                    id: draftSource
                    objectName: "atomicDraftSource"
                    font.family: Theme.monoFont
                    font.pixelSize: 14
                    wrapMode: TextEdit.Wrap
                    selectByMouse: true
                    textFormat: TextEdit.PlainText
                    placeholderText: "Question Markdown, then a line containing ---, then answer Markdown"
                    padding: 12
                    background: Rectangle {
                        color: Theme.surface
                        border.color: draftSource.activeFocus ? Theme.accent : Theme.rule
                        radius: 6
                    }
                    readOnly: dialog.applying
                    onTextChanged: dialog.updateDraft()
                }
            }
        }
        Label {
            textFormat: Text.PlainText
            visible: dialog.editingSet
            text: dialog.drafts.length < 2 ? "Add at least two cards before replacing the original."
                : "The replacement cards will be new cards in the same deck, with cloze groups numbered from 1 on each card. Review history is retained."
            color: Theme.inkMuted
            wrapMode: Text.Wrap
            font.pixelSize: 11
            Layout.fillWidth: true
        }
    }
    Dialog {
        id: instructionsDialog
        title: "Atomicize instructions"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        focus: true
        closePolicy: Popup.NoAutoClose
        width: Math.min(760, dialog.ui.width - 2 * dialog.ui.gutter)
        height: Math.min(520, dialog.ui.height - 36)
        padding: 20
        function openEditor() {
            instructionsSource.text = dialog.generationInstructions
            open()
            instructionsSource.forceActiveFocus()
        }
        background: Rectangle { color: Theme.surface; border.color: Theme.rule; radius: 10 }
        header: Item {
            implicitHeight: 60
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                Label { text: instructionsDialog.title; color: Theme.ink; font.pixelSize: 21; Layout.fillWidth: true }
                GlyphButton { glyph: "close"; hint: "Close instructions without saving"; onClicked: instructionsDialog.close() }
            }
        }
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                text: "Additional instructions for the next proposal (optional)"
                font.family: Theme.uiFont
                color: Theme.ink
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Label {
                text: "Describe which concepts to separate or keep together. These instructions are sent to the selected model along with this card. Saving them does not start generation."
                color: Theme.inkMuted
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                TextArea {
                    id: instructionsSource
                    objectName: "atomicInstructions"
                    textFormat: TextEdit.PlainText
                    wrapMode: TextEdit.Wrap
                    selectByMouse: true
                    font.family: Theme.contentFont
                    font.pixelSize: 17
                    padding: 12
                    placeholderText: "For example: Separate atomicity, durability, and rollback into standalone cards about database transactions."
                    Accessible.name: "Additional Atomicize instructions"
                    background: Rectangle { color: Theme.editorField; border.color: instructionsSource.activeFocus ? Theme.accent : Theme.rule; radius: 6 }
                }
            }
            Label {
                text: instructionsSource.length + " / 4000 characters"
                color: instructionsSource.length > 4000 ? Theme.error : Theme.inkMuted
                font.family: Theme.monoFont
            }
        }
        footer: RowLayout {
            Item { Layout.fillWidth: true }
            AppButton { text: "Cancel"; onClicked: instructionsDialog.close() }
            AppButton {
                text: "Save instructions"
                primary: true
                enabled: instructionsSource.length <= 4000
                onClicked: {
                    dialog.generationInstructions = instructionsSource.text
                    instructionsDialog.close()
                }
            }
        }
    }
    footer: RowLayout {
        spacing: 10
        Item {
            Layout.fillWidth: true
        }
        AppButton {
            objectName: "atomicCancel"
            text: "Close"
            enabled: !atomic.busy
            opacity: atomic.busy ? 0.45 : 1
            onClicked: dialog.close()
        }
        AppButton {
            objectName: "atomicApply"
            visible: dialog.editingSet
            text: dialog.applying ? "Replacing" : "Replace with " + dialog.drafts.length + " cards"
            primary: true
            enabled: dialog.splitReady && !atomic.busy && !app.busy
            onClicked: dialog.submit()
        }
    }
}
