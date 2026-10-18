import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml

Dialog {
    id: detail
    required property var ui
    property string cardId: ""
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
    width: Math.min(1120, ui.width - 2 * ui.gutter)
    height: Math.min(800, ui.height - 2 * ui.gutter)
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

    function openFor(id) { cardId = id; open() }
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
    onClosed: { requestId = ""; loading = false; ui.detailOpen = false }
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
                    text: detail.width < 900 ? "Generate" : "Generate like this"
                    glyph: "cards"
                    hint: "Draft a varied card testing the same concept with the configured language model. Review it before saving."
                    enabled: !app.busy
                    onClicked: detail.ui.openGenerateLike(detail.cardId)
                }
                AppButton {
                    text: detail.width < 620 ? "Narrate" : "Record narration"
                    glyph: "mic"
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
            id: detailBody
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: detailBody.availableWidth
                height: Math.max(640, implicitHeight, detailBody.availableHeight)
                spacing: 14
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.rule }
                ScrollView {
                    id: cardContents
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 80
                    Layout.preferredHeight: 180
                    clip: true
                    contentWidth: availableWidth
                    GridLayout {
                        width: cardContents.availableWidth
                        columns: detail.width < 720 ? 1 : 2
                        columnSpacing: 30
                        rowSpacing: 20
                        Repeater {
                            model: [{label: "Front", side: "front", source: detail.card.front || ""}, {label: "Back", side: "back", source: detail.card.back || ""}]
                            delegate: ColumnLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignTop
                                Layout.preferredWidth: (cardContents.availableWidth - 30) / 2
                                spacing: 14
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Label { text: modelData.label; font.pixelSize: 15; font.bold: true; color: Theme.inkMuted }
                                    NarrationWarning {
                                        shown: modelData.side === "front" ? detail.narrationWarnings.front : detail.narrationWarnings.back
                                        reason: "A recording may no longer match this " + modelData.label.toLowerCase() + " text. Check the affected forms in Record narration."
                                    }
                                    Item { Layout.fillWidth: true }
                                }
                                MarkdownPane {
                                    markdown: modelData.source
                                    mediaRoot: media.rootPath
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: implicitHeight
                                    baseFontSize: 19
                                }
                            }
                        }
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.rule }
                Flow {
                    visible: detail.summary.reviewCount !== undefined
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    spacing: 24
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
                            spacing: 6
                            Label { text: modelData.label; color: Theme.inkMuted; font.pixelSize: 11 }
                            Label { text: modelData.value; color: modelData.color || Theme.ink; font.pixelSize: 14; font.bold: true }
                            HoverHandler { id: statHover }
                            ToolTip.visible: statHover.hovered
                            ToolTip.text: modelData.hint
                        }
                    }
                }
                RowLayout {
                    visible: !!detail.loadError.code
                    Layout.fillWidth: true
                    Label {
                        text: (detail.loadError.code || "") + ": " + (detail.loadError.message || "")
                        color: Theme.error; wrapMode: Text.Wrap; Layout.fillWidth: true
                    }
                    AppButton { text: "Retry"; onClicked: detail.load(detail.nextOffset > 0) }
                }
                CalendarTrace {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 362
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
