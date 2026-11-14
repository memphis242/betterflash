import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml
import QtCore

Dialog {
    id: detail
    required property var ui
    property string cardId: ""
    property int activeSide: 0
    property bool chatOpen: false
    property bool chatTab: false
    property bool chatResumePending: false
    property string chatPausedVariant: ""
    readonly property bool compactChat: chatOpen && availableWidth < 900
    readonly property real cardPaneWidth: cardContents.width
    Settings {
        id: chatLayoutSettings
        category: "cardChatPane"
        property real paneWidth: 420
    }
    function showChat() {
        if (ui.page === 0 && app.reviewing && !app.paused && app.busy) {
            ui.showError("CARD_CHAT_BUSY", "Finish the current operation before opening a chat during review.", "The review timer must be paused before the chat opens.")
            return
        }
        if (!cardChat.openFor(cardId)) {
            const failure = cardChat.errorInfo
            ui.showError(failure.code || "CARD_CHAT_OPEN", (failure.message || "Could not open this card's chats.") + "\n" + (failure.action || ""), failure.detail || "")
            return
        }
        if (cardChat.cardId !== cardId) return
        if (ui.page === 0 && app.reviewing && !app.paused) {
            const variant = app.currentCard.variantId || ""
            app.pauseReview()
            if (app.paused) {
                chatResumePending = true
                chatPausedVariant = variant
            }
        }
        chatOpen = true
        chatTab = true
        Qt.callLater(chatPane.focusComposer)
    }
    function hideChat() {
        cardChat.close()
        chatOpen = false
        Qt.callLater(resumeChatReview)
    }
    function resumeChatReview() {
        if (!chatResumePending || chatOpen || ui.modalOpen || app.busy) return
        chatResumePending = false
        if (ui.page === 0 && app.reviewing && app.paused && (app.currentCard.variantId || "") === chatPausedVariant) app.resumeReview()
    }

    property bool traceExpanded: true
    property var contentPositions: ({})
    readonly property bool actionMenuOpen: cardMenu.visible
    readonly property bool chatConfirmationOpen: chatPane.modalDialogOpen
    readonly property var card: ui.cardById(cardId)
    readonly property var weaknessAssessment: {
        if (!card || !card.id) return ({})
        if (card.weakness && typeof card.weakness === "object") return card.weakness
        const variants = card.variantWeaknesses
        if (!variants || typeof variants !== "object") return ({})
        const ranks = ({ unassessed: 0, steady: 1, watch: 2, fragile: 3, "needs-study": 4 })
        return Object.keys(variants).map(key => variants[key]).filter(value => value && typeof value === "object")
            .sort((a, b) => (ranks[b.tier] || 0) - (ranks[a.tier] || 0) || Number(b.score || 0) - Number(a.score || 0))[0] || ({})
    }
    readonly property var narrationWarnings: card && card.id && ui.cardNarrationController
        ? ui.cardNarrationController.warningsForCard(card) : ({hasStale: false, front: false, back: false, count: 0})
    property var recordedEvents: []
    property var summary: ({})
    property var loadError: ({})
    property bool loading: false
    property bool hasMore: false
    property int nextOffset: 0
    property int serial: 0
    property string requestId: ""
    property double now: Date.now()
    property double openedAt: Date.now()
    readonly property var timelineEvents: {
        if (summary.reviewCount === undefined) return []
        const events = recordedEvents.slice()
        if (card.due) events.push({id: "next-due", kind: "due", at: card.due,
            label: new Date(card.due).getTime() <= openedAt ? "Due now" : "Next review"})
        if (events.length) events.push({id: "now", kind: "now", at: new Date(openedAt).toISOString()})
        return events.sort((a, b) => new Date(a.at).getTime() - new Date(b.at).getTime() || a.id.localeCompare(b.id))
    }
    readonly property string countdown: {
        const due = new Date(card.due || "").getTime()
        if (!Number.isFinite(due)) return "Not scheduled"
        const minutes = Math.ceil((due - now) / 60000)
        if (minutes <= 0) return "Due now"
        const days = Math.floor(minutes / 1440)
        const hours = Math.floor(minutes % 1440 / 60)
        return "In " + (days ? days + "d " : "") + (hours ? hours + "h " : "") + minutes % 60 + "m"
    }
    readonly property color averageRatingColor: {
        const grade = Number(summary.averageGrade)
        if (!summary.reviewCount || !Number.isFinite(grade)) return Theme.inkMuted
        return [Theme.recallMissed, Theme.recallPartial, Theme.recallHard,
            Theme.recallGood, Theme.recallEasy][Math.max(0, Math.min(4, Math.round(grade)))]
    }
    readonly property int recordedDays: summary.firstRecordedAt
        ? Math.max(0, Math.floor((now - new Date(summary.firstRecordedAt).getTime()) / 86400000)) : 0
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(chatOpen ? 2000 : 1560, ui.width - 2 * ui.gutter)
    height: Math.min(900, ui.height - 2 * ui.gutter)
    padding: width < 650 ? 14 : 24
    modal: true
    focus: true
    closePolicy: chatOpen ? Popup.NoAutoClose : Popup.CloseOnPressOutside
    background: Rectangle { radius: 16; color: Theme.surface; border.color: Theme.ledgerRule }
    Overlay.modal: Rectangle { color: Theme.scrim }

    Instantiator {
        model: ["undo", "redo", "redoAlternate"]
        delegate: Shortcut {
            required property string modelData
            sequence: shortcuts.bindings[modelData] || ""
            context: Qt.WindowShortcut
            autoRepeat: false
            enabled: detail.visible && detail.ui.historyActionAllowed(modelData)
            onActivated: detail.ui.runAction(modelData)
        }
    }

    function keyboardContext() {
        return visible && ui.popupOwnsKeyboardFocus(detail) && !actionMenuOpen && !chatConfirmationOpen && !ui.historyBlockingModalOpen
    }
    readonly property bool writableTextFocus: {
        const item = ui.activeFocusItem
        return !!item && item.text !== undefined && item.cursorPosition !== undefined && item.readOnly !== true
    }
    function requestEdit() {
        if (!card.id || app.busy || actionMenuOpen || chatConfirmationOpen) return
        if (chatOpen && cardChat.busy) {
            ui.showError("CARD_CHAT_BUSY", "A chat request is running. Wait for it or cancel it in Ask AI before editing this card.", "Card: " + cardId)
            return
        }
        if (chatOpen && cardChat.dirty) {
            ui.showError("CARD_CHAT_UNSAVED", "The chat has an unsaved reply. Retry saving it or explicitly discard it in Ask AI before editing this card.", "Card: " + cardId)
            return
        }
        const id = cardId
        ui.selectedCardId = id
        close()
        ui.openCardEditor(id)
    }
    Shortcut {
        sequence: "E"
        context: Qt.WindowShortcut
        autoRepeat: false
        enabled: detail.keyboardContext() && !detail.writableTextFocus && !!detail.card.id && !app.busy
        onActivated: detail.requestEdit()
    }
    Shortcut {
        sequence: "Escape"
        context: Qt.WindowShortcut
        autoRepeat: false
        enabled: detail.keyboardContext() && !detail.chatOpen
        onActivated: detail.close()
    }

    Shortcut {
        sequences: ["Ctrl+Tab", "Ctrl+Shift+Tab"]
        context: Qt.WindowShortcut
        autoRepeat: false
        enabled: detail.keyboardContext() && !detail.writableTextFocus
        onActivated: detail.activeSide = (detail.activeSide + 1) % sideTabs.count
    }

    function rememberContentPosition() {
        if (!cardId) return
        contentPositions[cardId] = {
            side: activeSide,
            front: frontContents.contentItem.contentY,
            back: backContents.contentItem.contentY
        }
    }
    function restoreContentPosition(id) {
        if (cardId !== id || !visible) return
        const saved = contentPositions[id] || {side: 0, front: 0, back: 0}
        activeSide = saved.side
        const restore = (view, position) => {
            const area = view.contentItem
            area.contentY = Math.max(0, Math.min(position, area.contentHeight - area.height))
        }
        restore(frontContents, saved.front)
        restore(backContents, saved.back)
    }
    function openFor(id) {
        const alreadyOpen = visible
        if (alreadyOpen) rememberContentPosition()
        if (id !== cardId && chatOpen) hideChat()
        cardId = id
        activeSide = (contentPositions[id] || {}).side || 0
        if (alreadyOpen) reload()
        else open()
        Qt.callLater(() => restoreContentPosition(id))
    }
    function load(older) {
        if (loading || !cardId || !visible) return
        loadError = ({})
        loading = true
        requestId = String(++serial)
        app.loadCardTimeline(cardId, older ? nextOffset : 0, requestId)
    }
    function reload() {
        loading = false
        recordedEvents = []
        summary = ({})
        nextOffset = 0
        hasMore = false
        openedAt = Date.now()
        now = openedAt
        load(false)
    }
    onOpened: reload()
    onClosed: { if (chatOpen) hideChat(); rememberContentPosition(); requestId = ""; loading = false; ui.detailOpen = false }
    Timer { interval: 1000; repeat: true; running: detail.visible; onTriggered: detail.now = Date.now() }
    Timer { id: refresh; interval: 100; onTriggered: if (detail.visible) detail.reload() }
    Connections {
        target: app
        function onCardsChanged() {
            if (!detail.visible) return
            if (!detail.card.id) {
                if (detail.chatOpen && cardChat.dirty && cardChat.cardId === detail.cardId) return
                detail.close()
            } else refresh.restart()
        }
        function onCardTimelineReady(id, result) {
            if (!detail.visible || id !== detail.cardId || result.requestId !== detail.requestId) return
            if (result.error && result.error.code) {
                detail.loadError = result.error
                detail.loading = false
                return
            }
            const merged = result.offset > 0 ? result.events.concat(detail.recordedEvents) : result.events
            const seen = new Set()
            detail.recordedEvents = merged.filter(event => {
                if (seen.has(event.id)) return false
                seen.add(event.id)
                return true
            })
            detail.summary = result
            detail.hasMore = result.hasMore
            detail.nextOffset = result.nextOffset
            detail.loading = false
        }
    }

    Connections {
        target: detail.ui
        function onModalOpenChanged() { Qt.callLater(detail.resumeChatReview) }
    }
    Connections {
        target: app
        function onBusyChanged() { detail.resumeChatReview() }
    }
    contentItem: ColumnLayout {
        spacing: 10
        TabBar {
            visible: detail.compactChat
            Layout.fillWidth: true
            currentIndex: detail.chatTab ? 1 : 0
            onCurrentIndexChanged: detail.chatTab = currentIndex === 1
            AppTabButton { text: "Card" }
            AppTabButton { text: "Ask AI" }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8
            ColumnLayout {
                id: cardContents
                visible: !detail.compactChat || !detail.chatTab
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: detail.chatOpen && !detail.compactChat ? 400 : 0
                spacing: 14
                GridLayout {
                    Layout.fillWidth: true
                    columns: detail.cardPaneWidth < 900 ? 2 : 3
                    columnSpacing: 12
                    rowSpacing: 12
                    ColumnLayout {
                        Layout.row: 0
                        Layout.column: 0
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 5
                        Label { text: detail.card.id ? "Card detail" : "Card unavailable"; font.pixelSize: 20; font.bold: true; color: Theme.ink; Layout.fillWidth: true; elide: Text.ElideRight; Layout.minimumWidth: 0 }
                        WeaknessBadge { assessment: detail.weaknessAssessment; visible: !!assessment.assessed && (assessment.tier === "watch" || assessment.tier === "fragile" || assessment.tier === "needs-study") }
                        Label {
                            Layout.fillWidth: true
                            text: detail.ui.deckById(detail.card.deckId || "").name || ""
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: Theme.inkMuted
                            font.pixelSize: 12
                        }
                    }
                    Flow {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.preferredWidth: 650
                        Layout.row: detail.cardPaneWidth < 900 ? 1 : 0
                        Layout.column: detail.cardPaneWidth < 900 ? 0 : 1
                        Layout.columnSpan: detail.cardPaneWidth < 900 ? 2 : 1
                        Layout.alignment: Qt.AlignRight
                        spacing: 12
                        AppButton {
                            text: detail.cardPaneWidth < 500 ? "Edit" : "Edit card"
                            glyph: "edit"
                            primary: true
                            enabled: !!detail.card.id
                            hint: "Edit this card (E)"
                            onClicked: detail.requestEdit()
                        }
                        AppButton {
                            text: "Ask AI"
                            glyph: "robot"
                            hint: detail.chatOpen ? "Hide this card's chat pane" : "Open separate chats with this card as context. Only information retrieval tools are available."
                            enabled: !(detail.card.loadError && detail.card.loadError.code)
                            onClicked: detail.chatOpen ? detail.hideChat() : detail.showChat()
                        }
                        AppButton {
                            text: detail.cardPaneWidth < 700 ? "Recall & help" : "Recall & study help"
                            glyph: "book"
                            hint: "See this card's recall evidence, recall strength and suggested study steps. Ask the language model for help only if you choose."
                            enabled: !!detail.card.id && typeof detail.ui.openStudyGuidance === "function"
                            onClicked: detail.ui.openStudyGuidance(detail.cardId, "")
                        }
                        AppButton {
                            text: detail.cardPaneWidth < 900 ? "Generate" : "Generate similar card"
                            glyph: "cards"
                            hint: "Draft a varied card testing the same concept with the configured language model. Review it before saving."
                            enabled: !!detail.card.id && !app.busy && !(detail.card.loadError && detail.card.loadError.code)
                            onClicked: detail.ui.openGenerateSimilar(detail.cardId)
                        }
                        AppButton {
                            text: detail.cardPaneWidth < 620 ? "Narrate" : "Record narration"
                            glyph: "mic"
                            enabled: !!detail.card.id && !(detail.card.loadError && detail.card.loadError.code)
                            onClicked: detail.ui.openCardNarration(detail.cardId)
                        }
                        AppButton {
                            text: "Dismiss check follow-up"
                            glyph: "check"
                            visible: detail.ui.hasCheckFollowUp(detail.card)
                            enabled: !!detail.card.id && !app.busy && !(detail.card.loadError && detail.card.loadError.code)
                            hint: "Remove correctness and completeness follow-up marks. Keeps other tags and review schedules."
                            onClicked: detail.ui.dismissCheckFollowUp(detail.cardId)
                        }
                        NarrationWarning {
                            shown: detail.narrationWarnings.hasStale
                            reason: "A narration may no longer match the card text. Open Record narration and confirm each affected side."
                        }
                        GlyphButton {
                            glyph: "more"; hint: "Card actions"
                            onClicked: cardMenu.open()
                            CardActionsMenu {
                                id: cardMenu
                                y: parent.height
                                ui: detail.ui
                                cardId: detail.cardId
                                onBeforeAction: detail.close()
                            }
                        }
                    }
                    RowLayout {
                        Layout.row: 0
                        Layout.column: detail.cardPaneWidth < 900 ? 1 : 2
                        spacing: 12
                        HistoryActionButton {
                            objectName: "cardDetailUndoButton"
                            ui: detail.ui
                            visible: Theme.preferences.showUndoRedo
                            action: "undo"
                        }
                        HistoryActionButton {
                            objectName: "cardDetailRedoButton"
                            ui: detail.ui
                            visible: Theme.preferences.showUndoRedo
                            action: "redo"
                        }
                        GlyphButton { glyph: "close"; hint: "Close card detail"; onClicked: detail.close() }
                    }
                }
                ScrollView {
                    id: cardErrorViewport
                    visible: !!(detail.card.loadError || {}).code
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(detail.height < 650 ? 64 : 116, cardReadError.implicitHeight)
                    contentWidth: availableWidth
                    clip: true
                    ErrorBlock {
                        id: cardReadError
                        width: cardErrorViewport.availableWidth
                        code: (detail.card.loadError || {}).code || ""
                        message: (detail.card.loadError || {}).message || ""
                        details: (detail.card.loadError || {}).detail || ""
                    }
                }
                TabBar {
                    id: sideTabs
                    objectName: "cardDetailSideTabs"
                    Layout.fillWidth: true
                    currentIndex: detail.activeSide
                    onCurrentIndexChanged: detail.activeSide = currentIndex
                    AppTabButton { text: "Front" }
                    AppTabButton { text: "Back" }
                }
                ScrollView {
                    id: bodyViewport
                    objectName: "cardDetailBodyScroll"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 0
                    Layout.preferredHeight: 0
                    contentWidth: availableWidth
                    contentHeight: bodyColumn.implicitHeight
                    clip: true
                    ScrollBar.vertical.policy: contentHeight > availableHeight ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                    Column {
                        id: bodyColumn
                        objectName: "cardDetailBody"
                        width: bodyViewport.availableWidth
                        spacing: 12
                        ColumnLayout {
                            id: readingPane
                            width: bodyColumn.width
                            height: Math.max(Layout.minimumHeight, bodyViewport.availableHeight - evidencePane.implicitHeight - bodyColumn.spacing)
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumWidth: 0
                            Layout.minimumHeight: cardSides.Layout.minimumHeight
                                + (clozeKey.visible ? clozeKey.implicitHeight + spacing : 0)
                            Layout.preferredHeight: readingPane.Layout.minimumHeight
                            spacing: 8
                            StackLayout {
                                id: cardSides
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                Layout.minimumWidth: 0
                                Layout.minimumHeight: 200
                                currentIndex: detail.activeSide
                                ScrollView {
                                    id: frontContents
                                    objectName: "cardDetailFrontScroll"
                                    contentWidth: availableWidth
                                    clip: true
                                    ColumnLayout {
                                        width: frontContents.availableWidth
                                        spacing: 12
                                        NarrationWarning {
                                            shown: detail.narrationWarnings.front
                                            reason: "A recording may no longer match the front text. Check the affected forms in Record narration."
                                        }
                                        MarkdownPane {
                                            markdown: detail.card.front || ""
                                            clozePreview: detail.card.kind === "cloze"
                                            selectable: true
                                            mediaRoot: media.rootPath
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                            Layout.preferredHeight: implicitHeight
                                            baseFontSize: 19
                                        }
                                    }
                                }
                                ScrollView {
                                    id: backContents
                                    objectName: "cardDetailBackScroll"
                                    contentWidth: availableWidth
                                    clip: true
                                    ColumnLayout {
                                        width: backContents.availableWidth
                                        spacing: 12
                                        NarrationWarning {
                                            shown: detail.narrationWarnings.back
                                            reason: "A recording may no longer match the back text. Check the affected forms in Record narration."
                                        }
                                        MarkdownPane {
                                            markdown: detail.card.back || ""
                                            selectable: true
                                            mediaRoot: media.rootPath
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                            Layout.preferredHeight: implicitHeight
                                            baseFontSize: 19
                                        }
                                    }
                                }
                            }
                            ClozePreviewKey {
                                id: clozeKey
                                visible: detail.card.kind === "cloze"
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                Layout.minimumHeight: implicitHeight
                                Layout.maximumHeight: implicitHeight
                            }
                        }
                        ColumnLayout {
                            id: evidencePane
                            width: bodyColumn.width
                            height: implicitHeight
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.minimumHeight: implicitHeight
                            Layout.preferredHeight: implicitHeight
                            Layout.maximumHeight: implicitHeight
                            spacing: 8
                            RowLayout {
                                visible: !!detail.loadError.code
                                Layout.fillWidth: true
                                Layout.maximumHeight: implicitHeight
                                ErrorBlock {
                                    code: detail.loadError.code || ""
                                    message: detail.loadError.message || ""
                                    details: detail.loadError.detail || ""
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                }
                                AppButton { text: "Retry"; onClicked: detail.load(detail.nextOffset > 0) }
                            }
                            ColumnLayout {
                                id: statisticsGroup
                                objectName: "cardDetailStatisticsGroup"
                                Layout.fillWidth: true
                                Layout.minimumHeight: implicitHeight
                                Layout.preferredHeight: implicitHeight
                                Layout.maximumHeight: implicitHeight
                                Layout.topMargin: 4
                                spacing: 8
                                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.rule }
                                Label {
                                    text: "Review statistics"
                                    color: Theme.ink
                                    font.family: Theme.uiFont
                                    font.bold: true
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    elide: Text.ElideRight
                                }
                                ColumnLayout {
                                    id: statistics
                                    Layout.fillWidth: true
                                    Layout.minimumHeight: implicitHeight
                                    Layout.preferredHeight: implicitHeight
                                    Layout.maximumHeight: implicitHeight
                                    Label {
                                        visible: detail.summary.reviewCount === undefined
                                        text: detail.loading ? "Loading review statistics..." : "Review statistics are unavailable. Retry loading the card history."
                                        textFormat: Text.PlainText
                                        wrapMode: Text.Wrap
                                        color: Theme.inkMuted
                                        Layout.fillWidth: true
                                    }
                                    Flow {
                                        visible: detail.summary.reviewCount !== undefined
                                        Layout.fillWidth: true
                                        spacing: 18
                                        Repeater {
                                            model: [
                                                {label: "Next review", value: detail.countdown, hint: detail.card.due ? "Earliest scheduled variant: " + detail.ui.dateTime(detail.card.due) : "This card has no scheduled review."},
                                                {label: "Successful reviews", value: detail.summary.reviewCount ? detail.summary.successfulCount + " of " + detail.summary.reviewCount : "No reviews", hint: "Hard, Good and Easy count as successful. Partial recall is tracked separately."},
                                                {label: "Average recall", value: detail.summary.reviewCount ? Math.round(detail.summary.averageRecall * 100) + "%" : "Not yet reviewed", color: detail.averageRatingColor, hint: "Average recalled fraction, including partial recall. Color follows the average review rating rounded to Missed, Partial, Hard, Good or Easy."},
                                                {label: "Best time", value: detail.summary.hasSuccessfulReview ? Number(detail.summary.bestSuccessfulSeconds).toFixed(1) + " s" : "No qualifying review", hint: "Your time to beat: fastest recorded response graded Hard, Good or Easy, across this card's review variants. Missed and Partial reviews do not qualify."},
                                                {label: "Recorded history", value: detail.summary.firstRecordedAt ? detail.recordedDays + " days" : "Unknown", hint: "Time since the earliest retained save or review. Original creation may be earlier."}
                                            ]
                                            delegate: Column {
                                                required property var modelData
                                                width: Math.min(statistics.width, Math.max(145, valueLabel.implicitWidth))
                                                spacing: 6
                                                Label { text: modelData.label; color: Theme.inkMuted; font.pixelSize: 11 }
                                                Label {
                                                    id: valueLabel
                                                    width: parent.width
                                                    text: modelData.value
                                                    color: modelData.color || Theme.ink
                                                    font.family: Theme.monoFont
                                                    font.pixelSize: 14
                                                    font.bold: true
                                                    wrapMode: Text.Wrap
                                                }
                                                HoverHandler { id: statHover }
                                                ToolTip.visible: statHover.hovered
                                                ToolTip.text: modelData.hint
                                            }
                                        }
                                    }
                                }
                                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.rule }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.maximumHeight: implicitHeight
                                Label {
                                    text: "Recall trace"
                                    color: Theme.ink
                                    font.family: Theme.uiFont
                                    font.bold: true
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    elide: Text.ElideRight
                                }
                                GlyphButton {
                                    objectName: "cardDetailTraceToggle"
                                    glyph: detail.traceExpanded ? "down" : "up"
                                    hint: detail.traceExpanded ? "Collapse recall trace" : "Expand recall trace"
                                    onClicked: detail.traceExpanded = !detail.traceExpanded
                                }
                            }
                            CalendarTrace {
                                id: recallTrace
                                visible: detail.traceExpanded
                                Layout.fillWidth: true
                                Layout.fillHeight: false
                                Layout.minimumHeight: 362
                                Layout.preferredHeight: 362
                                Layout.maximumHeight: 362
                                historyKey: detail.cardId
                                historyStart: detail.summary.firstRecordedAt || ""
                                events: detail.timelineEvents
                                loading: detail.loading
                                hasMore: detail.hasMore
                                canLoadEarlier: !detail.loadError.code
                                onLoadOlderRequested: detail.load(true)
                            }
                        }
                    }
                }
            }
            Rectangle {
                id: chatDivider
                visible: detail.chatOpen && !detail.compactChat
                Layout.fillHeight: true
                Layout.preferredWidth: 8
                color: Theme.transparent
                activeFocusOnTab: true
                Rectangle { anchors.centerIn: parent; height: parent.height; width: chatDivider.activeFocus ? 2 : 1; color: chatDivider.activeFocus ? Theme.accent : Theme.rule }
                Accessible.role: Accessible.Separator
                Accessible.name: "Resize card chat pane"
                function resizeBy(delta) {
                    chatLayoutSettings.paneWidth = Math.max(330, Math.min(600, detail.availableWidth - 408, chatLayoutSettings.paneWidth + delta))
                }
                Keys.onLeftPressed: resizeBy(20)
                Keys.onRightPressed: resizeBy(-20)
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.SplitHCursor
                    property real lastX: 0
                    onPressed: mouse => { lastX = mapToItem(detail.contentItem, mouse.x, mouse.y).x; chatDivider.forceActiveFocus() }
                    onPositionChanged: mouse => {
                        if (!pressed) return
                        const nextX = mapToItem(detail.contentItem, mouse.x, mouse.y).x
                        chatDivider.resizeBy(lastX - nextX)
                        lastX = nextX
                    }
                }
            }
            CardChatPane {
                id: chatPane
                ui: detail.ui
                visible: detail.chatOpen && (!detail.compactChat || detail.chatTab)
                Layout.fillHeight: true
                Layout.fillWidth: detail.compactChat
                Layout.minimumWidth: detail.compactChat ? 0 : 330
                Layout.preferredWidth: detail.compactChat ? detail.availableWidth : Math.max(330, Math.min(chatLayoutSettings.paneWidth, detail.availableWidth - 408))
                Layout.maximumWidth: detail.compactChat ? Infinity : 600
                onCloseRequested: detail.hideChat()
            }
        }
    }
}
