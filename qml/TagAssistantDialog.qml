pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    required property var ui
    property string deckId: ""
    property string cardId: ""
    property bool includeSubdecks: true
    property var savedVocabulary: []
    property var selectedIds: []
    property int anchorIndex: -1
    property int detailIndex: -1
    property bool saving: false
    property string saveRequest: ""
    property string applyRequest: ""
    property string notice: ""
    property var saveError: ({})
    property var pendingVocabulary: []
    readonly property var liveDeck: ui.deckById(deckId)
    readonly property bool vocabularyChanged: !liveDeck.id || !!liveDeck.tagVocabularyError
        || JSON.stringify(Array.from(liveDeck.tagVocabulary || [])) !== JSON.stringify(savedVocabulary)
    onVocabularyChangedChanged: if (vocabularyChanged && tagAssistant.busy) tagAssistant.cancel()
    readonly property var rows: tagAssistant.results
    readonly property var current: detailIndex >= 0 && detailIndex < rows.length ? rows[detailIndex] : ({})
    readonly property var eligible: rows.filter(row => !row.stale && !row.errorInfo?.message && (row.tags || []).length > 0)
    readonly property var selectedRows: eligible.filter(row => selectedIds.indexOf(row.snapshot.id) >= 0)
    readonly property bool locked: tagAssistant.busy || saving
    readonly property var failure: saveError.message ? saveError : tagAssistant.errorInfo
    title: cardId ? "Suggest card tags" : "Deck tags"
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    width: Math.min(1100, ui.width - 2 * ui.gutter)
    height: Math.min(920, ui.height - 2 * ui.gutter)
    padding: width < 600 ? 14 : 20
    closePolicy: Popup.NoAutoClose
    background: Rectangle { color: Theme.surface; radius: 12; border.color: Theme.rule }
    Overlay.modal: Rectangle { color: Theme.scrim }

    function openFor(id, targetCard) {
        if (locked) return
        tagAssistant.clear()
        deckId = id
        cardId = targetCard
        includeSubdecks = !targetCard
        savedVocabulary = (ui.deckById(id).tagVocabulary || []).slice()
        vocabulary.text = savedVocabulary.join("\n")
        selectedIds = []
        detailIndex = -1
        anchorIndex = -1
        notice = ""
        saveError = ({})
        tabs.currentIndex = targetCard && savedVocabulary.length ? 1 : 0
        tagAssistant.prepare(deckId, cardId, includeSubdecks)
        open()
    }
    function draftTags() {
        return vocabulary.text.split(/[\n,]/).map(value => value.trim()).filter(value => value.length > 0)
    }
    function prepareRun() {
        selectedIds = []
        detailIndex = -1
        anchorIndex = -1
        notice = ""
        saveError = ({})
        tagAssistant.prepare(deckId, cardId, includeSubdecks)
    }
    function saveVocabulary() {
        pendingVocabulary = draftTags()
        notice = ""
        saveError = ({})
        saveRequest = "tag-vocabulary-" + Date.now()
        saving = true
        if (!app.setDeckTagVocabulary(deckId, pendingVocabulary, saveRequest, savedVocabulary)) {
            saving = false
            saveError = app.lastError
        }
    }
    function applySelected() {
        notice = ""
        saveError = ({})
        applyRequest = "tag-assignments-" + Date.now()
        saving = true
        if (!app.applySuggestedTags(selectedRows.map(row => ({ snapshot: row.snapshot, tags: row.tags })), applyRequest, deckId, savedVocabulary)) {
            saving = false
            saveError = app.lastError
        }
    }
    function choose(index, modifiers) {
        const id = rows[index].snapshot.id
        if (modifiers & Qt.ShiftModifier && anchorIndex >= 0) {
            const range = rows.slice(Math.min(index, anchorIndex), Math.max(index, anchorIndex) + 1).map(row => row.snapshot.id)
            selectedIds = modifiers & Qt.ControlModifier ? Array.from(new Set(selectedIds.concat(range))) : range
        } else if (modifiers & Qt.ControlModifier) {
            selectedIds = selectedIds.indexOf(id) >= 0 ? selectedIds.filter(value => value !== id) : selectedIds.concat([id])
            anchorIndex = index
        } else {
            selectedIds = [id]
            anchorIndex = index
        }
        detailIndex = index
    }
    onClosed: tagAssistant.clear()
    Connections {
        target: tagAssistant
        function onChanged() {
            if (dialog.detailIndex < 0 && dialog.rows.length) dialog.detailIndex = 0
        }
    }
    Connections {
        target: app
        function onDeckTagVocabularySaved(id, requestId, success) {
            if (requestId !== dialog.saveRequest || id !== dialog.deckId) return
            dialog.saving = false
            if (success) {
                dialog.savedVocabulary = dialog.pendingVocabulary.slice()
                tagAssistant.prepare(dialog.deckId, dialog.cardId, dialog.includeSubdecks)
                dialog.selectedIds = []
                dialog.detailIndex = -1
                dialog.notice = "Deck vocabulary saved. Card tags are unchanged."
            } else dialog.saveError = app.lastError
        }
        function onSuggestedTagsApplied(requestId, success) {
            if (requestId !== dialog.applyRequest) return
            dialog.saving = false
            if (success) {
                dialog.notice = "Suggested tags saved. Use the cards pane's tag filter to find them."
                dialog.selectedIds = []
            } else dialog.saveError = app.lastError
        }
    }
    header: Item {
        implicitHeight: 64
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: dialog.padding
            anchors.rightMargin: dialog.padding
            Label { text: dialog.title; color: Theme.ink; font.bold: true; font.pixelSize: 23; Layout.fillWidth: true }
            GlyphButton { glyph: "close"; enabled: !dialog.saving; hint: "Close and cancel pending model requests. Unsaved suggestions are discarded."; onClicked: dialog.close() }
        }
    }
    contentItem: ColumnLayout {
        spacing: 12
        Label {
            text: dialog.ui.deckPath(dialog.deckId)
            textFormat: Text.PlainText
            font.pixelSize: 17
            font.bold: true
            color: Theme.ink
            Layout.fillWidth: true
            elide: Text.ElideRight
        }
        Label {
            text: "The model proposes tags for your review. Suggestions can be wrong. Card text is sent to the configured model; web search is not used."
            color: Theme.inkMuted
            font.pixelSize: 14
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        RowLayout {
            visible: dialog.vocabularyChanged
            Layout.fillWidth: true
            ErrorBlock {
                message: dialog.liveDeck.tagVocabularyError || ""
                Layout.fillWidth: true
            }
            Label {
                visible: !dialog.liveDeck.tagVocabularyError
                text: "The deck vocabulary changed or the deck was deleted. Reload it before running or applying suggestions."
                textFormat: Text.PlainText
                color: Theme.warning
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            AppButton {
                text: "Reload vocabulary"
                enabled: !dialog.locked && !!dialog.liveDeck.id && !dialog.liveDeck.tagVocabularyError
                hint: "Replace the unsaved vocabulary draft with the current saved list and discard pending suggestions."
                onClicked: {
                    dialog.savedVocabulary = Array.from(dialog.liveDeck.tagVocabulary || [])
                    vocabulary.text = dialog.savedVocabulary.join("\n")
                    dialog.prepareRun()
                }
            }
        }
        AppSwitch {
            visible: !dialog.cardId
            text: "Include subdecks"
            checked: dialog.includeSubdecks
            enabled: !dialog.locked
            onToggled: {
                dialog.includeSubdecks = checked
                dialog.prepareRun()
            }
        }
        TabBar {
            id: tabs
            Layout.fillWidth: true
            AppTabButton { text: "Deck vocabulary"; enabled: !dialog.locked }
            AppTabButton { text: "Card tagging"; enabled: !dialog.locked }
        }
        Label {
            text: (tagAssistant.status || "Ready.") + "\n" + tagAssistant.completed + " of " + tagAssistant.total
                + " cards processed. Model: " + (ai.provider === "local" ? ai.localModelLabel : ai.model)
            textFormat: Text.PlainText
            color: Theme.ink
            font.pixelSize: 15
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        ModelCacheControl { operation: tagAssistant; policyEnabled: !dialog.locked; Layout.fillWidth: true }
        ProgressBar { visible: tagAssistant.busy; from: 0; to: Math.max(1, tagAssistant.total); value: tagAssistant.completed; Layout.fillWidth: true }
        Rectangle {
            visible: !!dialog.failure.message
            Layout.fillWidth: true
            implicitHeight: Math.min(errorBlock.implicitHeight, 160) + 24
            color: Theme.errorSurface
            border.color: Theme.error
            radius: 6
            ScrollView {
                id: errorScroll
                anchors.fill: parent
                anchors.margins: 12
                clip: true
                contentWidth: availableWidth
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ErrorBlock {
                    id: errorBlock
                    width: errorScroll.availableWidth
                    code: dialog.failure.code || ""
                    message: dialog.failure.message || ""
                    action: dialog.failure.action || ""
                    details: dialog.failure.detail || ""
                    textSize: 15
                }
            }
        }
        StackLayout {
            currentIndex: tabs.currentIndex
            Layout.fillWidth: true
            Layout.fillHeight: true
            ScrollView {
                id: vocabularyScroll
                contentWidth: availableWidth
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ColumnLayout {
                    width: vocabularyScroll.availableWidth
                    spacing: 12
                    Label {
                        text: "Allowed tags for this deck"
                        color: Theme.ink
                        font.bold: true
                        font.pixelSize: 17
                    }
                    Label {
                        text: "Enter one tag per line (up to 100 tags, 80 characters each). Save this vocabulary before tagging cards. Editing the vocabulary does not remove tags from existing cards."
                        color: Theme.inkMuted
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 180
                        contentWidth: availableWidth
                        clip: true
                        TextArea {
                            id: vocabulary
                            enabled: !dialog.locked
                            selectByMouse: true
                            wrapMode: TextEdit.Wrap
                            color: Theme.ink
                            font.family: Theme.uiFont
                            font.pixelSize: 16
                            padding: 12
                            placeholderText: "For example: transactions, concurrency, recovery"
                            Accessible.name: "Allowed deck tags, one per line"
                            background: Rectangle { color: Theme.editorField; border.color: vocabulary.activeFocus ? Theme.accent : Theme.rule; radius: 6 }
                        }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 10
                        AppButton { text: dialog.saving ? "Saving..." : "Save vocabulary"; enabled: !dialog.locked && !app.busy && !dialog.vocabularyChanged; onClicked: dialog.saveVocabulary() }
                        AppButton {
                            text: "Propose tags from deck cards"
                            hint: "Scan every card in scope, one model request per card. Proposed tags need your review before saving."
                            enabled: !dialog.locked && !app.busy && !dialog.vocabularyChanged && tagAssistant.total > 0
                            onClicked: {
                                dialog.prepareRun()
                                tagAssistant.prepare(dialog.deckId, "", dialog.includeSubdecks)
                                tagAssistant.startVocabulary(dialog.draftTags())
                            }
                        }
                    }
                    Label {
                        text: "A scan sends each card in this deck" + (dialog.includeSubdecks ? " and its subdecks" : "") + " to the model separately and can incur usage charges. Existing tags and your vocabulary guide the suggestions. Cards added later need a new scan."
                        color: Theme.inkMuted
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Label {
                        visible: tagAssistant.suggestedVocabulary.length > 0
                        text: "Proposed additions" + (tagAssistant.completed < tagAssistant.total ? " (partial scan)" : "")
                        color: Theme.ink
                        font.bold: true
                        font.pixelSize: 17
                    }
                    TextEdit {
                        visible: tagAssistant.suggestedVocabulary.length > 0
                        text: tagAssistant.suggestedVocabulary.join(", ")
                        textFormat: TextEdit.PlainText
                        color: Theme.ink
                        font.family: Theme.uiFont
                        font.pixelSize: 16
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.Wrap
                        Layout.fillWidth: true
                        Layout.preferredHeight: contentHeight
                    }
                    AppButton {
                        visible: tagAssistant.suggestedVocabulary.length > 0
                        text: "Add proposals to editable vocabulary"
                        enabled: !dialog.locked && !tagAssistant.vocabularyStale
                        onClicked: {
                            const seen = new Set()
                            vocabulary.text = dialog.draftTags().concat(tagAssistant.suggestedVocabulary).filter(tag => {
                                const key = tag.toLocaleLowerCase()
                                if (seen.has(key)) return false
                                seen.add(key)
                                return true
                            }).join("\n")
                            dialog.notice = "Proposed tags added to the draft. Edit the list and Save vocabulary when ready."
                        }
                    }
                    Label {
                        visible: !!tagAssistant.vocabularyStale
                        text: "Scanned cards changed. Run the scan again before accepting its proposals."
                        color: Theme.warning
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }
            }
            ColumnLayout {
                spacing: 10
                Label {
                    text: dialog.savedVocabulary.length ? "Uses saved vocabulary: " + dialog.savedVocabulary.join(", ") : "Add and save a deck vocabulary first. The model will select only from that list."
                    textFormat: Text.PlainText
                    color: Theme.inkMuted
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                AppButton {
                    text: dialog.cardId ? "Suggest tags for this card" : "Suggest tags for cards"
                    enabled: !dialog.locked && !app.busy && !dialog.vocabularyChanged && dialog.savedVocabulary.length > 0 && tagAssistant.total > 0
                    Layout.fillWidth: true
                    hint: "One model request per card. Existing tags are kept. Review assignments before applying."
                    onClicked: { dialog.prepareRun(); tagAssistant.startTagging(dialog.savedVocabulary) }
                }
                RowLayout {
                    visible: dialog.rows.length > 0
                    Layout.fillWidth: true
                    AppButton {
                        text: "Select all suggestions"
                        enabled: !dialog.locked
                        onClicked: dialog.selectedIds = dialog.eligible.map(row => row.snapshot.id)
                    }
                    Label { text: dialog.selectedRows.length + " selected"; color: Theme.inkMuted; Layout.fillWidth: true }
                }
                GridLayout {
                    columns: dialog.width < 700 ? 1 : 2
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    columnSpacing: 14
                    ListView {
                        id: resultList
                        model: dialog.rows
                        clip: true
                        spacing: 4
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 380
                        Layout.preferredHeight: 150
                        ScrollBar.vertical: ScrollBar {}
                        currentIndex: dialog.detailIndex
                        Keys.onReturnPressed: event => { if (currentIndex >= 0) dialog.choose(currentIndex, event.modifiers) }
                        Keys.onSpacePressed: event => { if (currentIndex >= 0) dialog.choose(currentIndex, event.modifiers) }
                        delegate: ItemDelegate {
                            id: row
                            required property var modelData
                            required property int index
                            width: resultList.width
                            implicitHeight: rowContent.implicitHeight + 20
                            highlighted: dialog.selectedIds.indexOf(modelData.snapshot.id) >= 0
                            contentItem: ColumnLayout {
                                id: rowContent
                                Label { text: dialog.ui.compactText(row.modelData.snapshot.front || ""); textFormat: Text.PlainText; color: Theme.ink; maximumLineCount: 2; elide: Text.ElideRight; wrapMode: Text.Wrap; Layout.fillWidth: true }
                                Label { text: row.modelData.stale ? "Card changed - run again" : row.modelData.errorInfo?.message ? "Could not tag" : (row.modelData.tags || []).join(", ") || "No matching tags"; textFormat: Text.PlainText; color: row.modelData.stale || row.modelData.errorInfo?.message ? Theme.warning : Theme.inkMuted; wrapMode: Text.Wrap; Layout.fillWidth: true }
                            }
                            background: Rectangle { color: row.highlighted ? Theme.accentSoft : Theme.surfaceRaised; border.color: row.highlighted ? Theme.accent : Theme.rule; radius: 4 }
                            MouseArea { anchors.fill: parent; onClicked: mouse => { resultList.forceActiveFocus(); dialog.choose(row.index, mouse.modifiers) } }
                        }
                    }
                    ScrollView {
                        id: detailScroll
                        clip: true
                        contentWidth: availableWidth
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 540
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                        ColumnLayout {
                            width: detailScroll.availableWidth
                            spacing: 12
                            ErrorBlock {
                                message: dialog.current.errorInfo?.message || ""
                                code: dialog.current.errorInfo?.code || ""
                                action: dialog.current.errorInfo?.action || ""
                                details: dialog.current.errorInfo?.detail || ""
                                textSize: 15
                                Layout.fillWidth: true
                            }
                            TextEdit {
                                Layout.fillWidth: true
                                text: dialog.current.snapshot ? (dialog.current.snapshot.front || "") + "\n\n" + (dialog.current.snapshot.back || "")
                                    + "\n\nSuggested tags: " + ((dialog.current.tags || []).join(", ") || "None")
                                    + "\n\n" + (dialog.current.reason || "") : "Suggested tags and their reasons will appear here."
                                textFormat: TextEdit.PlainText
                                readOnly: true
                                selectByMouse: true
                                wrapMode: TextEdit.Wrap
                                font.family: Theme.uiFont
                                font.pixelSize: 16
                                color: Theme.ink
                            }
                        }
                    }
                }
                Label { text: "Click a card to select it; Ctrl-click adds cards; Shift-click selects a range. Only unchanged cards with suggestions can be applied. Existing tags and review schedules are preserved."; color: Theme.inkMuted; font.pixelSize: 13; wrapMode: Text.Wrap; Layout.fillWidth: true }
            }
        }
        Label { visible: !!dialog.notice; text: dialog.notice; textFormat: Text.PlainText; color: Theme.ink; wrapMode: Text.Wrap; Layout.fillWidth: true }
    }
    footer: Flow {
        padding: 12
        spacing: 10
        AppButton {
            visible: tagAssistant.busy
            text: "Cancel remaining requests"
            fill: Theme.cancelActionFill
            hoverFill: Theme.cancelActionHover
            outline: Theme.error
            onClicked: tagAssistant.cancel()
        }
        AppButton {
            visible: tabs.currentIndex === 1 && dialog.rows.length > 0
            text: dialog.saving ? "Saving tags..." : "Apply selected suggestions (" + dialog.selectedRows.length + ")"
            enabled: !dialog.locked && !app.busy && !dialog.vocabularyChanged && dialog.selectedRows.length > 0
            fill: Theme.checkActionFill
            hoverFill: Theme.checkActionHover
            outline: Theme.checkActionOutline
            foreground: Theme.checkActionInk
            onClicked: dialog.applySelected()
        }
        AppButton { text: "Close"; enabled: !dialog.saving; onClicked: dialog.close() }
    }
}
