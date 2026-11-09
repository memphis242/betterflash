import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml

Dialog {
    id: detail
    required property var ui
    property string cardId: ""
    property int activeSide: 0
    property bool traceExpanded: true
    property bool statsExpanded: true
    property var contentPositions: ({})
    property real evidenceHeight: -1
    readonly property bool evidenceCollapsed: !traceExpanded && !statsExpanded
    readonly property real collapsedEvidenceHeight: 112 + (loadError.code ? 88 : 0)
    readonly property bool actionMenuOpen: cardMenu.visible
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
    width: Math.min(1240, ui.width - 2 * ui.gutter)
    height: Math.min(900, ui.height - 2 * ui.gutter)
    padding: width < 650 ? 14 : 24
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
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
    function resizeEvidence() {
        if (!evidencePane || !bodySplit) return
        const statisticsHeight = Math.min(170, statistics.implicitHeight) + 8
        const expandedHeight = evidenceHeight >= 0 ? evidenceHeight : (bodySplit.height - 12) / 2
        const desiredHeight = collapsedEvidenceHeight
            + (statsExpanded ? statisticsHeight : 0)
            + (traceExpanded ? Math.max(160, expandedHeight - collapsedEvidenceHeight - statisticsHeight) : 0)
        evidencePane.SplitView.preferredHeight = Math.min(desiredHeight,
            Math.max(collapsedEvidenceHeight, bodySplit.height - 152))
    }
    onTraceExpandedChanged: resizeEvidence()
    onStatsExpandedChanged: resizeEvidence()
    onLoadErrorChanged: resizeEvidence()
    function openFor(id) {
        const alreadyOpen = visible
        if (alreadyOpen) rememberContentPosition()
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
    onOpened: { resizeEvidence(); reload() }
    onClosed: { rememberContentPosition(); requestId = ""; loading = false; ui.detailOpen = false }
    Timer { interval: 1000; repeat: true; running: detail.visible; onTriggered: detail.now = Date.now() }
    Timer { id: refresh; interval: 100; onTriggered: if (detail.visible) detail.reload() }
    Connections {
        target: app
        function onCardsChanged() {
            if (!detail.visible) return
            if (!detail.card.id) detail.close()
            else refresh.restart()
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

    contentItem: ColumnLayout {
        spacing: 14
        GridLayout {
            Layout.fillWidth: true
            columns: detail.width < 900 ? 2 : 3
            columnSpacing: 12
            rowSpacing: 12
            ColumnLayout {
                Layout.row: 0
                Layout.column: 0
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 5
                Label { text: "Card detail"; font.pixelSize: 20; font.bold: true; color: Theme.ink; Layout.fillWidth: true; elide: Text.ElideRight; Layout.minimumWidth: 0 }
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
                Layout.row: detail.width < 900 ? 1 : 0
                Layout.column: detail.width < 900 ? 0 : 1
                Layout.columnSpan: detail.width < 900 ? 2 : 1
                Layout.alignment: Qt.AlignRight
                spacing: 12
                AppButton {
                    text: detail.width < 500 ? "Edit" : "Edit card"
                    glyph: "edit"
                    primary: true
                    onClicked: { detail.close(); detail.ui.openCardEditor(detail.cardId) }
                }
                AppButton {
                    text: detail.width < 700 ? "Recall & help" : "Recall & study help"
                    glyph: "book"
                    hint: "See this card's recall evidence, recall strength and suggested study steps. Ask the language model for help only if you choose."
                    enabled: typeof detail.ui.openStudyGuidance === "function"
                    onClicked: detail.ui.openStudyGuidance(detail.cardId, "")
                }
                AppButton {
                    text: detail.width < 900 ? "Generate" : "Generate similar card"
                    glyph: "cards"
                    hint: "Draft a varied card testing the same concept with the configured language model. Review it before saving."
                    enabled: !app.busy && !(detail.card.loadError && detail.card.loadError.code)
                    onClicked: detail.ui.openGenerateSimilar(detail.cardId)
                }
                AppButton {
                    text: detail.width < 620 ? "Narrate" : "Record narration"
                    glyph: "mic"
                    enabled: !(detail.card.loadError && detail.card.loadError.code)
                    onClicked: detail.ui.openCardNarration(detail.cardId)
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
                Layout.column: detail.width < 900 ? 1 : 2
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
            contentHeight: bodySplit.height
            clip: true
            ScrollBar.vertical.policy: contentHeight > availableHeight ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
            SplitView {
                id: bodySplit
                objectName: "cardDetailSplit"
                width: bodyViewport.availableWidth
                height: Math.max(bodyViewport.availableHeight,
                    cardSides.SplitView.minimumHeight + evidencePane.SplitView.minimumHeight + 12)
                orientation: Qt.Vertical
                onHeightChanged: detail.resizeEvidence()
                onResizingChanged: {
                    if (!resizing && !detail.evidenceCollapsed) {
                        detail.evidenceHeight = evidencePane.height
                            + (detail.statsExpanded ? 0 : Math.min(170, statistics.implicitHeight) + 8)
                    }
                }
                handle: Rectangle {
                    id: splitHandle
                    implicitWidth: 12
                    implicitHeight: 12
                    color: Theme.transparent
                    activeFocusOnTab: true
                    Accessible.name: "Resize card content and recall evidence"
                    ToolTip.visible: SplitHandle.hovered || activeFocus
                    ToolTip.text: "Drag to resize, or focus this divider and use Up/Down; Home restores equal space"
                    Keys.onPressed: event => {
                        if (event.key === Qt.Key_Home) {
                            detail.evidenceHeight = -1
                            detail.resizeEvidence()
                            event.accepted = true
                            return
                        }
                        if (event.key !== Qt.Key_Up && event.key !== Qt.Key_Down) return
                        const amount = event.key === Qt.Key_Up ? 24 : -24
                        const height = Math.max(evidencePane.SplitView.minimumHeight,
                            Math.min(bodySplit.height - 152, evidencePane.height + amount))
                        detail.evidenceHeight = height
                            + (detail.statsExpanded ? 0 : Math.min(170, statistics.implicitHeight) + 8)
                        evidencePane.SplitView.preferredHeight = height
                        event.accepted = true
                    }
                    Rectangle {
                        anchors.centerIn: parent
                        width: parent.width
                        height: 2
                        color: splitHandle.SplitHandle.hovered || splitHandle.SplitHandle.pressed || splitHandle.activeFocus ? Theme.accent : Theme.rule
                    }
                }
                StackLayout {
                    id: cardSides
                    SplitView.fillWidth: true
                    SplitView.fillHeight: true
                    SplitView.minimumWidth: 0
                    SplitView.minimumHeight: 140
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
                ColumnLayout {
                    id: evidencePane
                    SplitView.minimumWidth: 0
                    SplitView.minimumHeight: detail.collapsedEvidenceHeight
                        + (detail.traceExpanded ? 160 : 0) + (detail.statsExpanded ? 36 : 0)
                    spacing: 8
                    RowLayout {
                        visible: !!detail.loadError.code
                        Layout.fillWidth: true
                        Label {
                            text: (detail.loadError.code || "") + ": " + (detail.loadError.message || "")
                            textFormat: Text.PlainText
                            color: Theme.error
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            maximumLineCount: 4
                            elide: Text.ElideRight
                            ToolTip.visible: timelineErrorHover.hovered
                            ToolTip.text: text
                            HoverHandler { id: timelineErrorHover }
                        }
                        AppButton { text: "Retry"; onClicked: detail.load(detail.nextOffset > 0) }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: "Review statistics"
                            color: Theme.ink
                            font.family: Theme.uiFont
                            font.bold: true
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            elide: Text.ElideRight
                        }
                        GlyphButton {
                            objectName: "cardDetailStatsToggle"
                            glyph: detail.statsExpanded ? "up" : "down"
                            hint: detail.statsExpanded ? "Collapse review statistics" : "Expand review statistics"
                            onClicked: detail.statsExpanded = !detail.statsExpanded
                        }
                    }
                    ScrollView {
                        id: statsViewport
                        visible: detail.statsExpanded
                        Layout.fillWidth: true
                        Layout.fillHeight: !detail.traceExpanded
                        Layout.minimumHeight: visible ? 36 : 0
                        Layout.preferredHeight: Math.min(170, statistics.implicitHeight)
                        contentWidth: availableWidth
                        clip: true
                        ColumnLayout {
                            id: statistics
                            width: statsViewport.availableWidth
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
                    }
                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.rule }
                    RowLayout {
                        Layout.fillWidth: true
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
                            glyph: detail.traceExpanded ? "up" : "down"
                            hint: detail.traceExpanded ? "Collapse recall trace" : "Expand recall trace"
                            onClicked: detail.traceExpanded = !detail.traceExpanded
                        }
                    }
                    ScrollView {
                        id: traceViewport
                        visible: detail.traceExpanded
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: visible ? 40 : 0
                        Layout.preferredHeight: 362
                        contentWidth: availableWidth
                        clip: true
                        ScrollBar.vertical.policy: availableHeight < 342 ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                        CalendarTrace {
                            width: traceViewport.availableWidth
                            height: Math.max(342, traceViewport.availableHeight)
                            historyKey: detail.cardId
                            historyStart: detail.summary.firstRecordedAt || ""
                            events: detail.timelineEvents
                            loading: detail.loading
                            hasMore: detail.hasMore
                            canLoadEarlier: !detail.loadError.code
                            onLoadOlderRequested: detail.load(true)
                        }
                    }
                    Item { visible: detail.evidenceCollapsed; Layout.fillHeight: true }
                }
            }
        }
    }
}
