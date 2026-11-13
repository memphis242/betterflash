pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    required property var ui
    property string deckId: ""
    property string checkMode: "correctness"
    property bool started: false
    property bool marking: false
    property bool hasMarked: false
    property bool resumeReview: false
    property string markRequest: ""
    property string markMessage: ""
    property var selectedIds: []
    property int anchorIndex: -1
    property int detailIndex: -1
    readonly property var rows: deckChecker.results
    readonly property var current: detailIndex >= 0 && detailIndex < rows.length ? rows[detailIndex] : ({})
    readonly property var report: current.report || ({})
    readonly property var failure: current.errorInfo || ({})
    readonly property int scopeCount: {
        const children = new Map()
        for (const deck of app.decks) {
            const siblings = children.get(deck.parentId) || []
            siblings.push(deck.id)
            children.set(deck.parentId, siblings)
        }
        const ids = new Set([deckId])
        const pending = [deckId]
        for (let index = 0; index < pending.length; ++index) {
            for (const child of children.get(pending[index]) || []) {
                if (!ids.has(child)) { ids.add(child); pending.push(child) }
            }
        }
        return app.cards.filter(card => ids.has(card.deckId)).length
    }
    title: checkMode === "correctness" ? "Check deck correctness" : "Check deck completeness"
    objectName: "deckCheckDialog"
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

    function openFor(id, mode) {
        if (deckChecker.busy || cardChecker.busy || marking) return
        deckChecker.clear()
        deckId = id
        checkMode = mode
        started = false
        selectedIds = []
        detailIndex = -1
        anchorIndex = -1
        markMessage = ""
        hasMarked = false
        open()
    }
    function run() {
        selectedIds = []
        detailIndex = -1
        anchorIndex = -1
        markMessage = ""
        started = true
        if (app.reviewing && !app.paused) {
            resumeReview = true
            app.pauseReview()
        }
        deckChecker.start(deckId, checkMode)
    }
    function choose(index, modifiers) {
        const id = rows[index].cardId
        if (modifiers & Qt.ShiftModifier && anchorIndex >= 0) {
            const range = rows.slice(Math.min(anchorIndex, index), Math.max(anchorIndex, index) + 1).map(row => row.cardId)
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
    function markSelected() {
        const snapshots = rows.filter(row => selectedIds.indexOf(row.cardId) >= 0 && !row.stale && !row.errorInfo?.message).map(row => row.snapshot)
        if (!snapshots.length) return
        markMessage = ""
        markRequest = "deck-check-" + Date.now()
        marking = true
        if (!app.markCardsForCheckReview(snapshots, checkMode, markRequest)) {
            marking = false
            markMessage = app.lastError.message || "Could not queue the follow-up tags. Try again when the collection is available."
        }
    }
    readonly property int markableCount: rows.filter(row => selectedIds.indexOf(row.cardId) >= 0 && !row.stale && !row.errorInfo?.message).length
    function resumeOwnedReview() {
        if (visible || !resumeReview || app.busy) return
        resumeReview = false
        if (app.reviewing && app.paused) app.resumeReview()
    }
    onDetailIndexChanged: detailScroll.contentItem.contentY = 0
    onClosed: {
        deckChecker.clear()
        resumeOwnedReview()
    }
    Connections {
        target: deckChecker
        function onChanged() {
            if (dialog.detailIndex < 0 && dialog.rows.length) dialog.detailIndex = 0
        }
    }
    Connections {
        target: app
        function onBusyChanged() { dialog.resumeOwnedReview() }
        function onCheckReviewMarked(requestId, success) {
            if (requestId !== dialog.markRequest) return
            dialog.marking = false
            dialog.markMessage = success
                ? "Follow-up tags saved. Use the cards pane's tag filter to find check-" + dialog.checkMode + ". Dismiss follow-up from Card actions or card detail when resolved."
                : (app.lastError.message || "Follow-up tags were not saved. Check the collection error and try again.")
            if (success) { dialog.selectedIds = []; dialog.hasMarked = true }
        }
    }
    header: Item {
        implicitHeight: 64
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: dialog.padding
            anchors.rightMargin: dialog.padding
            Label { text: dialog.title; color: Theme.ink; font.pixelSize: 23; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
            GlyphButton {
                glyph: "close"
                enabled: !dialog.marking
                hint: deckChecker.busy ? "Close and cancel remaining checks. Saved follow-up tags are kept." : "Close results. Saved follow-up tags are kept."
                onClicked: dialog.close()
            }
        }
    }
    contentItem: ColumnLayout {
        spacing: 12
        Label {
            text: (ui.deckById(dialog.deckId).name || "Deck") + " and all subdecks"
            textFormat: Text.PlainText
            color: Theme.ink
            font.pixelSize: 17
            font.bold: true
            Layout.fillWidth: true
            elide: Text.ElideRight
        }
        Label {
            text: "LLMs can be wrong, have a knowledge cutoff, and miss recent changes. Attached images are not inspected. Findings are suggestions for your review."
            color: Theme.warning; font.pixelSize: 14; wrapMode: Text.Wrap; Layout.fillWidth: true
        }
        Label {
            text: !dialog.started
                ? dialog.scopeCount + " cards will be checked individually for " + dialog.checkMode + ". "
                    + (webSearch.enabled ? "Web search is enabled: up to 3 Tavily searches per card may use your quota. " : "Web search is off; checks use the model's intrinsic knowledge. ")
                    + "Each card uses one or more model requests. Model: " + (ai.provider === "local" ? ai.localModelLabel : ai.model)
                : deckChecker.status
            textFormat: Text.PlainText
            color: Theme.ink; font.pixelSize: 16; font.bold: dialog.started; wrapMode: Text.Wrap; Layout.fillWidth: true
        }
        Label {
            visible: dialog.started
            text: "Results cover the cards present when this run started. Cards added or moved into this deck or its subdecks later need a new run."
            color: Theme.inkMuted
            font.pixelSize: 13
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        ModelCacheControl {
            operation: deckChecker
            policyEnabled: !deckChecker.busy
            Layout.fillWidth: true
        }
        ProgressBar {
            visible: dialog.started
            from: 0; to: Math.max(1, deckChecker.total); value: deckChecker.completed
            Layout.fillWidth: true
        }
        RowLayout {
            Layout.fillWidth: true
            AppButton {
                visible: !deckChecker.busy
                text: dialog.started ? "Run again" : "Start check"
                enabled: dialog.scopeCount > 0 && !app.busy && !cardChecker.busy
                fill: Theme.checkActionFill; hoverFill: Theme.checkActionHover; outline: Theme.checkActionOutline; foreground: Theme.checkActionInk
                Layout.fillWidth: true
                onClicked: dialog.run()
            }
            AppButton {
                visible: deckChecker.busy
                text: "Cancel remaining checks"
                fill: Theme.cancelActionFill; hoverFill: Theme.cancelActionHover; outline: Theme.error
                onClicked: deckChecker.cancel()
            }
            Label {
                visible: dialog.started
                text: deckChecker.completed + " of " + deckChecker.total + " processed; " + dialog.rows.length + " need attention"
                color: Theme.inkMuted; font.family: Theme.monoFont; font.pixelSize: 13; wrapMode: Text.Wrap; Layout.fillWidth: true
            }
        }
        ErrorBlock {
            visible: !!deckChecker.errorInfo.message
            Layout.fillWidth: true
            code: deckChecker.errorInfo.code || ""
            message: deckChecker.errorInfo.message || ""
            action: deckChecker.errorInfo.action || ""
            details: deckChecker.errorInfo.detail || ""
        }
        Label {
            visible: dialog.started && dialog.rows.length === 0
            text: deckChecker.busy ? "Flagged cards will appear here as each check finishes." : "No flagged cards in the completed checks. This does not guarantee correctness or completeness."
            color: Theme.inkMuted; wrapMode: Text.Wrap; Layout.fillWidth: true
        }
        GridLayout {
            columns: dialog.width < 700 ? 1 : 2
            columnSpacing: 16
            rowSpacing: 10
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: dialog.rows.length > 0
            ColumnLayout {
                Layout.preferredWidth: dialog.width < 700 ? -1 : dialog.width * 0.34
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: dialog.width < 700 ? 160 : -1
                Layout.maximumHeight: dialog.width < 700 ? 180 : Infinity
                Label { text: "Cards needing attention"; color: Theme.ink; font.bold: true }
                ListView {
                    id: resultList
                    model: dialog.rows
                    clip: true
                    spacing: 4
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    ScrollBar.vertical: ScrollBar {}
                    keyNavigationEnabled: true
                    currentIndex: dialog.detailIndex
                    Keys.onSpacePressed: event => { if (currentIndex >= 0) dialog.choose(currentIndex, event.modifiers) }
                    Keys.onReturnPressed: event => { if (currentIndex >= 0) dialog.choose(currentIndex, event.modifiers) }
                    onCurrentIndexChanged: if (activeFocus) dialog.detailIndex = currentIndex
                    delegate: ItemDelegate {
                        id: resultRow
                        required property var modelData
                        required property int index
                        width: resultList.width
                        implicitHeight: rowContent.implicitHeight + 20
                        highlighted: dialog.selectedIds.indexOf(modelData.cardId) >= 0
                        contentItem: ColumnLayout {
                            id: rowContent
                            spacing: 6
                            Label { text: ui.compactText(resultRow.modelData.front); textFormat: Text.PlainText; color: Theme.ink; font.bold: true; wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight; Layout.fillWidth: true }
                            Label {
                                text: resultRow.modelData.errorInfo?.message ? "Could not check" : resultRow.modelData.stale ? "Card changed - check again" : resultRow.modelData.report?.verdict === "uncertain" ? "Uncertain" : "Possible issues"
                                color: Theme.warning; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true
                            }
                        }
                        background: Rectangle { color: resultRow.highlighted ? Theme.accentSoft : Theme.surfaceRaised; border.color: resultRow.highlighted ? Theme.accent : Theme.rule; border.width: resultRow.highlighted || dialog.detailIndex === resultRow.index ? 2 : 1; radius: 4 }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: mouse => { resultList.forceActiveFocus(); dialog.choose(resultRow.index, mouse.modifiers) }
                        }
                    }
                }
                Label { text: "Click to select; Ctrl-click adds cards; Shift-click selects a range."; color: Theme.inkMuted; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }
            }
            ScrollView {
                id: detailScroll
                clip: true
                contentWidth: availableWidth
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: dialog.width * 0.6
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ColumnLayout {
                    width: detailScroll.availableWidth
                    spacing: 12
                    Label { text: ui.deckPath(dialog.current.deckId || ""); textFormat: Text.PlainText; color: Theme.inkMuted; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    Label { text: ui.compactText(dialog.current.front || ""); textFormat: Text.PlainText; color: Theme.ink; font.bold: true; font.pixelSize: 18; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    Label { visible: !!dialog.current.stale; text: "This card changed or was deleted after the check began. Check its current content again before marking it."; color: Theme.warning; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    ErrorBlock { visible: !!dialog.failure.message; Layout.fillWidth: true; code: dialog.failure.code || ""; message: dialog.failure.message || ""; action: dialog.failure.action || ""; details: dialog.failure.detail || "" }
                    TextEdit { text: dialog.report.summary || ""; textFormat: TextEdit.PlainText; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; font.family: Theme.uiFont; font.pixelSize: 17; color: Theme.warning; Layout.fillWidth: true }
                    Repeater {
                        model: dialog.report.findings || []
                        delegate: ColumnLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 8
                            Rectangle { height: 1; color: Theme.rule; Layout.fillWidth: true }
                            Label { text: modelData.claim; textFormat: Text.PlainText; color: Theme.ink; font.bold: true; wrapMode: Text.Wrap; Layout.fillWidth: true }
                            TextEdit {
                                text: modelData.explanation + (modelData.suggestion ? "\n\nSuggested adjustment: " + modelData.suggestion : "") + ((modelData.sourceIds || []).length ? "\n\nSources: " + modelData.sourceIds.join(", ") : "")
                                textFormat: TextEdit.PlainText; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; font.family: Theme.uiFont; font.pixelSize: 15; color: Theme.ink; Layout.fillWidth: true
                            }
                        }
                    }
                    Label {
                        visible: !!dialog.report.verdict
                        text: dialog.report.searchCount > 0 ? "Web searches used: " + dialog.report.searchCount + (dialog.report.evidenceTruncated ? ". Evidence was shortened to fit the model." : "") : "No web search was performed for this card."
                        color: Theme.inkMuted; font.pixelSize: 13; wrapMode: Text.Wrap; Layout.fillWidth: true
                    }
                    Repeater {
                        model: dialog.report.sources || []
                        delegate: AppButton {
                            required property var modelData
                            text: modelData.id + ": " + modelData.title
                            hint: modelData.url
                            Layout.fillWidth: true
                            onClicked: Qt.openUrlExternally(modelData.url)
                        }
                    }
                }
            }
        }
        Item { visible: dialog.rows.length === 0; Layout.fillHeight: true }
        Label { visible: !!dialog.markMessage; text: dialog.markMessage; color: Theme.ink; wrapMode: Text.Wrap; Layout.fillWidth: true }
    }
    footer: Flow {
        padding: 12
        spacing: 10
        AppButton {
            visible: dialog.rows.length > 0
            text: dialog.marking ? "Saving follow-up tags..." : "Mark selected for follow-up (" + dialog.markableCount + ")"
            hint: "Adds check-" + dialog.checkMode + " to selected, unchanged cards with model findings. Failed checks cannot be marked. This does not change review schedules."
            enabled: !dialog.marking && !app.busy && dialog.markableCount > 0
            fill: Theme.checkActionFill; hoverFill: Theme.checkActionHover; outline: Theme.checkActionOutline; foreground: Theme.checkActionInk
            onClicked: dialog.markSelected()
        }
        AppButton {
            visible: dialog.hasMarked
            text: "Show marked cards"
            enabled: !dialog.marking && !app.busy
            hint: "Open the cards pane filtered to check-" + dialog.checkMode + " in this deck and its subdecks."
            onClicked: {
                const id = dialog.deckId
                const mode = dialog.checkMode
                dialog.close()
                ui.showDeckCheckFollowUp(id, mode)
            }
        }
        AppButton { text: "Close"; enabled: !dialog.marking; onClicked: dialog.close() }
    }
}
