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
    property bool seeded: false
    property bool loading: false
    property bool preview: false
    property string localError: ""
    readonly property int draftIndex: atomicTabs.currentIndex - 1
    readonly property var currentDraft: draftIndex >= 0 && draftIndex < drafts.length ? drafts[draftIndex] : ({})
    readonly property bool splitReady: atomic.hasProposal && atomic.decision === "split" && drafts.length >= 2 && drafts.length <= 5

    title: "Atomicize card"
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    width: Math.min(900, ui.width - 2 * ui.gutter)
    height: Math.min(760, ui.height - 36)
    padding: ui.width < 600 ? 14 : 20
    closePolicy: applying ? Popup.NoAutoClose : Popup.CloseOnEscape
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
        drafts = []
        seeded = false
        preview = false
        localError = ""
        atomicTabs.currentIndex = 0
        open()
        atomic.propose(cardId)
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
        updateDraft()
        for (let i = 0; i < drafts.length; ++i) {
            if (!drafts[i].front.trim() || !drafts[i].back.trim()) {
                localError = "ATOMIC_DRAFT: Write a front and a back for every proposed card."
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
        }
    }
    Connections {
        target: atomic
        function onChanged() {
            if (!dialog.visible)
                return
            if (!dialog.seeded && atomic.hasProposal && atomic.decision === "split") {
                dialog.drafts = atomic.proposals.map(card => ({
                            front: card.front,
                            back: card.back,
                            pointCount: card.pointCount || 1
                        }))
                dialog.seeded = true
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
        Label {
            textFormat: Text.PlainText
            text: "The front and back are sent to " + ai.endpoint + " using " + ai.model + ". Related lists and procedures can stay together."
            color: Theme.inkMuted
            font.pixelSize: 11
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        RowLayout {
            visible: !ai.hasApiKey
            Layout.fillWidth: true
            Label {
                textFormat: Text.PlainText
                text: "Add a language model key in Settings to use this action."
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
        Label {
            textFormat: Text.PlainText
            visible: atomic.busy || atomic.status.length > 0
            text: dialog.applying ? "Replacing the original card" : atomic.status
            color: Theme.inkMuted
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            font.family: Theme.monoFont
            font.pixelSize: 11
        }
        ScrollView {
            id: reasonScroll
            objectName: "atomicReasonScroll"
            visible: atomic.decision.length > 0 && atomic.reason.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(reasonLabel.implicitHeight, dialog.ui.width < 600 ? 80 : 64)
            clip: true
            contentWidth: availableWidth
            Label {
                textFormat: Text.PlainText
                id: reasonLabel
                width: reasonScroll.availableWidth
                text: (atomic.decision === "keep" ? "Keep this card together. " : "") + atomic.reason
                color: Theme.ink
                wrapMode: Text.Wrap
            }
        }
        ErrorBlock {
            objectName: "atomicError"
            message: dialog.localError || atomic.error
            Layout.fillWidth: true
        }
        TabBar {
            id: atomicTabs
            objectName: "atomicTabs"
            Layout.fillWidth: true
            enabled: !dialog.applying
            onCurrentIndexChanged: dialog.loadDraft()
            TabButton {
                text: "Source"
                font.pixelSize: 12
                padding: 5
                width: atomicTabs.width / (dialog.drafts.length + 1)
            }
            Repeater {
                model: dialog.drafts.length
                delegate: TabButton {
                    required property int index
                    text: "Card " + (index + 1)
                    font.pixelSize: 12
                    padding: 5
                    width: atomicTabs.width / (dialog.drafts.length + 1)
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
                text: "Points"
                color: Theme.inkMuted
                ToolTip.visible: pointsHover.containsMouse
                ToolTip.text: "Answer points used to grade partial recall"
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
                    readOnly: dialog.applying
                    onTextChanged: dialog.updateDraft()
                }
            }
        }
        Label {
            textFormat: Text.PlainText
            visible: dialog.splitReady
            text: "The replacement cards will be new basic cards in the same deck. Review history is retained."
            color: Theme.inkMuted
            wrapMode: Text.Wrap
            font.pixelSize: 11
            Layout.fillWidth: true
        }
    }
    footer: RowLayout {
        spacing: 10
        Item {
            Layout.fillWidth: true
        }
        AppButton {
            objectName: "atomicCancel"
            text: atomic.busy && !dialog.applying ? "Cancel request" : dialog.splitReady ? "Cancel" : "Close"
            enabled: !dialog.applying
            onClicked: dialog.close()
        }
        AppButton {
            objectName: "atomicApply"
            visible: dialog.splitReady
            text: dialog.applying ? "Replacing" : "Replace with " + dialog.drafts.length + " cards"
            primary: true
            enabled: !atomic.busy && !dialog.applying
            onClicked: dialog.submit()
        }
    }
}
