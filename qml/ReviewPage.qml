import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml

Item {
    id: page
    required property var ui
    readonly property bool hasCard: !!app.currentCard.id
    readonly property bool reviewModalVisible: reviewDialog.visible
    property var timelineCompleted: []
    property var timelineCurrentCard: ({})
    property string timelineCurrentVariant: ""
    readonly property var queueStats: [
        { key: "due", label: "Due now", count: deckCount("dueCount"), hint: "Review variants due in this deck and its subdecks" },
        { key: "new", label: "New due", count: deckCount("newDueCount"), hint: "Due variants that have never been reviewed" },
        { key: "review", label: "Reviewed due", count: deckCount("reviewDueCount"), hint: "Due variants with a previous review" },
        { key: "later", label: "Due later", count: deckCount("laterCount"), hint: "Variants with a future review date" }
    ]
    function deckCount(key) {
        if (ui.selectedDeck.id)
            return ui.selectedDeck[key] || 0
        return app.decks.filter(deck => !deck.parentId).reduce((total, deck) => total + (deck[key] || 0), 0)
    }
    function openReviewModal() {
        if (app.reviewing)
            reviewDialog.open()
    }
    function dismissReviewModal() {
        if (reviewDialog.visible)
            reviewDialog.close()
    }
    function reviewSnippet(variant) {
        const prefix = "Ask the question that this answers based on deck context: \n\n"
        let question = variant.question || (variant.kind === "cloze" ? "" : variant.front || "")
        if (variant.variantKey === "reverse" && question.indexOf(prefix) === 0)
            question = question.slice(prefix.length)
        return ui.compactText(question) || "Image question"
    }
    function gradeColor(grade) {
        switch (grade) {
        case 0: return Theme.error
        case 1: return Theme.warning
        case 2: return Theme.warning
        case 3: return Theme.success
        case 4: return Theme.success
        default: return Theme.rule
        }
    }
    function gradeMark(grade) {
        switch (grade) {
        case 0: return "M"
        case 1: return "P"
        case 2: return "H"
        case 3: return "G"
        case 4: return "E"
        default: return ""
        }
    }
    function gradeName(grade) {
        switch (grade) {
        case 0: return "Missed"
        case 1: return "Partial"
        case 2: return "Hard"
        case 3: return "Good"
        case 4: return "Easy"
        default: return ""
        }
    }
    function reviewTimelineItems() {
        const items = []
        const completed = Math.min(timelineCompleted.length, 2)
        for (let index = 0; index < 2; ++index) {
            if (index < 2 - completed)
                items.push({ kind: "empty", index: index })
            else {
                const completedCard = timelineCompleted[index - (2 - completed)] || {}
                items.push({ kind: "previous", card: completedCard.card || {}, grade: completedCard.grade, index: index })
            }
        }
        if (app.pendingCards.length > 0)
            items.push({ kind: "current", card: app.pendingCards[0], index: 2 })
        const upcoming = app.pendingCards.slice(1, 6)
        for (let index = 0; index < upcoming.length; ++index)
            items.push({ kind: "upcoming", card: upcoming[index], distance: index + 1, index: index + 3 })
        return items
    }
    Connections {
        target: app
        function onReviewingChanged() {
            if (app.reviewing) {
                page.timelineCompleted = []
                page.timelineCurrentCard = app.currentCard
                page.timelineCurrentVariant = app.currentCard.variantId || ""
                page.openReviewModal()
            } else if (reviewDialog.visible)
                reviewDialog.close()
        }
        function onCurrentCardChanged() {
            const nextVariant = app.currentCard.variantId || ""
            if (page.timelineCurrentVariant && nextVariant && nextVariant !== page.timelineCurrentVariant) {
                const recorded = app.history.find(review => review.variantId === page.timelineCurrentVariant)
                if (recorded)
                    page.timelineCompleted = page.timelineCompleted.concat([{
                        card: page.timelineCurrentCard,
                        grade: recorded.grade
                    }]).slice(-2)
            }
            page.timelineCurrentCard = app.currentCard
            page.timelineCurrentVariant = nextVariant
        }
        function onPausedChanged() {
            if (app.reviewing && !app.paused)
                page.openReviewModal()
        }
    }
    Connections {
        target: voice
        function onEnabledChanged() {
            if (voice.enabled && app.reviewing)
                page.openReviewModal()
        }
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: page.ui.gutter
        spacing: 16
        RowLayout {
            objectName: "reviewTopBar"
            Layout.fillWidth: true
            spacing: 10
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                Label {
                    textFormat: Text.PlainText
                    text: page.ui.selectedDeck.name || "Review"
                    font.family: Theme.contentFont
                    font.pixelSize: page.ui.width < 600 ? 23 : 28
                    color: Theme.ink
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Label {
                    textFormat: Text.PlainText
                    visible: app.reviewing
                    text: app.reviewedCount + " of " + app.sessionTotal + " reviewed" + (app.paused ? " - paused" : "")
                    font.family: Theme.monoFont
                    font.pixelSize: 11
                    color: app.paused ? Theme.warning : Theme.inkMuted
                }
            }
        }
        Item {
            id: idle
            objectName: "reviewIdle"
            visible: true
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 20
                ScrollView {
                    id: idleScroll
                    objectName: "reviewDeckBrowserScroll"
                    visible: app.decks.length > 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(contentHeight, idle.height * 0.62)
                    clip: true
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: idleScroll.availableWidth
                        spacing: 20
                        DeckBrowser {
                            ui: page.ui
                            Layout.fillWidth: true
                        }
                        GridLayout {
                            objectName: "reviewStats"
                            visible: page.deckCount("variantCount") > 0
                            Layout.fillWidth: true
                            columns: width < 600 ? 2 : 4
                            columnSpacing: 12
                            rowSpacing: 12
                            Repeater {
                                model: page.queueStats
                                delegate: Rectangle {
                                    id: stat
                                    required property var modelData
                                    objectName: "queueStat" + modelData.key
                                    readonly property int count: modelData.count
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 90
                                    Layout.minimumWidth: 0
                                    color: Theme.surface
                                    radius: 4
                                    border.color: Theme.rule
                                    border.width: 1
                                    Accessible.name: modelData.label + ": " + count
                                    ToolTip.visible: statHover.hovered
                                    ToolTip.text: modelData.hint
                                    HoverHandler { id: statHover }
                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 14
                                        spacing: 6
                                        Label {
                                            textFormat: Text.PlainText
                                            text: stat.modelData.label
                                            color: Theme.inkMuted
                                            font.pixelSize: 11
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
                                        }
                                        Label {
                                            textFormat: Text.PlainText
                                            text: stat.count
                                            color: Theme.ink
                                            font.family: Theme.monoFont
                                            font.pixelSize: 23
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                Item {
                    id: actionZone
                    objectName: "reviewStartArea"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 112
                    ColumnLayout {
                        anchors.centerIn: parent
                        width: parent.width
                        spacing: 14
                        Label {
                            textFormat: Text.PlainText
                            visible: !app.decks.length
                            text: "Your decks will appear here."
                            color: Theme.inkMuted
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                        AppButton {
                            objectName: "reviewPrimary"
                            text: app.reviewing ? "Resume review" : !app.decks.length ? "Create deck" : page.deckCount("cardCount") === 0 ? "Add card" : "Start review"
                            hint: "Review due cards in the selected deck and its subdecks"
                            primary: true
                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredWidth: Math.min(340, actionZone.width)
                            Layout.preferredHeight: page.ui.width < 600 ? 72 : 86
                            font.pixelSize: page.ui.width < 600 ? 21 : 25
                            enabled: !app.busy && (app.reviewing || !app.decks.length || page.deckCount("cardCount") === 0 || page.deckCount("dueCount") > 0)
                            onClicked: {
                                if (app.reviewing) {
                                    page.openReviewModal()
                                    if (app.paused)
                                        app.resumeReview()
                                } else if (!app.decks.length)
                                    page.ui.openDeckEditor("")
                                else if (page.deckCount("cardCount") === 0)
                                    page.ui.openCardEditor("")
                                else
                                    app.startReview(app.selectedDeckId)
                            }
                        }
                        Label {
                            textFormat: Text.PlainText
                            visible: !app.reviewing && app.decks.length > 0 && page.deckCount("cardCount") > 0 && page.deckCount("dueCount") === 0
                            text: "No cards due in this deck."
                            color: Theme.inkMuted
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                    }
                }
            }
        }
    }
    Dialog {
        id: reviewDialog
        objectName: "reviewDialog"
        parent: Overlay.overlay
        modal: true
        focus: true
        anchors.centerIn: parent
        width: Math.min(1120, page.ui.width - 2 * page.ui.gutter)
        height: Math.min(780, page.ui.height - 2 * page.ui.gutter)
        padding: page.ui.width < 600 ? 14 : 22
        closePolicy: Popup.CloseOnEscape
        onClosed: {
            if (app.reviewing && !app.paused)
                app.pauseReview()
        }
        background: Rectangle {
            objectName: "reviewDialogSurface"
            color: Theme.surface
            radius: 16
            border.color: Theme.rule
            border.width: 1
        }
        Overlay.modal: Rectangle { color: Theme.scrim }
        contentItem: ColumnLayout {
            spacing: 14
            Instantiator {
                model: ui.shortcutCommands.filter(c => c.action.indexOf("editor") !== 0 && c.action !== "saveCard").map(c => c.action)
                delegate: Shortcut {
                    required property string modelData
                    sequence: shortcuts.bindings[modelData] || ""
                    context: Qt.WindowShortcut
                    enabled: reviewDialog.visible && ui.shortcutAllowed(modelData)
                    onActivated: ui.runAction(modelData)
                }
            }
            RowLayout {
                objectName: "reviewModalHeader"
                Layout.fillWidth: true
                spacing: 10
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3
                    Label {
                        textFormat: Text.PlainText
                        text: page.hasCard ? (app.currentCard.deckName || ui.selectedDeck.name || "Review") : "Review"
                        color: Theme.ink
                        font.family: Theme.contentFont
                        font.pixelSize: page.ui.width < 600 ? 21 : 27
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Label {
                        textFormat: Text.PlainText
                        text: app.reviewedCount + " of " + app.sessionTotal + " reviewed" + (app.paused ? " - paused" : "")
                        color: app.paused ? Theme.warning : Theme.inkMuted
                        font.family: Theme.monoFont
                        font.pixelSize: 11
                    }
                }
                AppButton {
                    objectName: "reviewPause"
                    text: app.paused ? "Resume" : "Pause"
                    hint: shortcuts.bindings.pause
                    primary: true
                    enabled: !app.busy
                    onClicked: app.paused ? app.resumeReview() : app.pauseReview()
                }
                GlyphButton {
                    objectName: "reviewClose"
                    glyph: "close"
                    hint: "Close review and pause"
                    onClicked: reviewDialog.close()
                }
                GlyphButton {
                    objectName: "reviewMenuButton"
                    glyph: "more"
                    hint: "Review actions"
                    onClicked: reviewMenu.open()
                    Menu {
                        id: reviewMenu
                        y: parent.height
                        MenuItem { text: "Edit current card"; onTriggered: ui.openCardEditor(app.currentCard.cardId || app.currentCard.id) }
                        MenuItem { text: "Summarize remaining cards"; enabled: app.queueCount > 0; onTriggered: ui.openSummary() }
                        MenuItem { text: "End review"; onTriggered: app.stopReview() }
                    }
                }
            }
            RowLayout {
                objectName: "reviewQueueBar"
                Layout.fillWidth: true
                spacing: 0
                ListView {
                    id: reviewQueuePreviewList
                    objectName: "reviewQueuePreview"
                    readonly property bool timelineMode: true
                    readonly property int previousCount: page.timelineCompleted.length
                    readonly property int upcomingCount: Math.min(5, Math.max(0, app.pendingCards.length - 1))
                    readonly property bool previousSpaceReserved: true
                    readonly property bool fadeEnds: true
                    readonly property bool previousCardsFaded: true
                    readonly property bool upcomingCardsHazy: true
                    readonly property bool previousOutlines: true
                    readonly property real slotWidth: Math.max(74, Math.min(158, (width - 48) / 7))
                    readonly property real leadingTrack: Math.max(0, width / 2 - (2 * (slotWidth + spacing) + slotWidth / 2))
                    visible: count > 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: page.ui.width < 600 ? 82 : 92
                    clip: true
                    orientation: ListView.Horizontal
                    interactive: false
                    keyNavigationEnabled: true
                    activeFocusOnTab: true
                    cacheBuffer: 10000
                    model: page.reviewTimelineItems()
                    spacing: 8
                    Accessible.name: "Upcoming review timeline"
                    currentIndex: app.pendingCards.length > 0 ? 2 : -1
                    header: Item {
                        width: reviewQueuePreviewList.leadingTrack
                        height: reviewQueuePreviewList.height
                    }
                    footer: Item {
                        width: reviewQueuePreviewList.leadingTrack
                        height: reviewQueuePreviewList.height
                    }
                    function centerCurrent() {
                        if (currentIndex >= 0 && currentIndex < count)
                            positionViewAtIndex(currentIndex, ListView.Center)
                    }
                    onCurrentIndexChanged: Qt.callLater(centerCurrent)
                    onCountChanged: Qt.callLater(centerCurrent)
                    onWidthChanged: Qt.callLater(centerCurrent)
                    Component.onCompleted: Qt.callLater(centerCurrent)
                    Keys.onLeftPressed: decrementCurrentIndex()
                    Keys.onRightPressed: incrementCurrentIndex()
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        readonly property var card: modelData.card || {}
                        readonly property string variantId: card.variantId || card.id || ""
                        readonly property bool isPrevious: modelData.kind === "previous"
                        readonly property bool isCurrent: modelData.kind === "current"
                        readonly property bool isUpcoming: modelData.kind === "upcoming"
                        readonly property int reviewedGrade: isPrevious ? Number(modelData.grade) : -1
                        readonly property bool reviewOutline: isPrevious
                        readonly property string timelineRole: modelData.kind
                        objectName: isCurrent ? "reviewQueueCurrent" : isUpcoming ? "reviewQueueItem" + (modelData.index - 3) : "reviewTimelineItem" + index
                        width: isUpcoming
                               ? Math.max(42, reviewQueuePreviewList.slotWidth * (1 - 0.22 * (modelData.distance - 1)))
                               : reviewQueuePreviewList.slotWidth
                        height: isCurrent ? 72 : 62
                        ToolTip.visible: queueHover.hovered
                        ToolTip.text: isPrevious ? "Reviewed " + page.gradeName(reviewedGrade) + ": " + page.reviewSnippet(card) : page.reviewSnippet(card)
                        Accessible.name: isPrevious ? "Reviewed " + page.gradeName(reviewedGrade) + " card: " + page.reviewSnippet(card) : isCurrent ? "Current card: " + page.reviewSnippet(card) : "Upcoming card: " + page.reviewSnippet(card)
                        visible: modelData.kind !== "empty"
                        opacity: isPrevious ? 0.52 : isCurrent ? 1 : Math.max(0.18, 0.58 - ((modelData.distance - 1) * 0.10))
                        color: isCurrent ? Theme.surfaceRaised : Theme.canvas
                        border.color: isPrevious ? page.gradeColor(modelData.grade) : isCurrent ? Theme.accent : Theme.rule
                        border.width: isCurrent ? 2 : 1
                        radius: isCurrent ? 7 : 5
                        HoverHandler { id: queueHover }
                        Label {
                            anchors.fill: parent
                            anchors.margins: 8
                            textFormat: Text.PlainText
                            text: modelData.kind === "empty" ? "" : modelData.kind === "previous" ? page.reviewSnippet(card) : modelData.kind === "current" ? "Current\n" + page.reviewSnippet(card) : page.reviewSnippet(card)
                            color: isCurrent ? Theme.ink : Theme.inkMuted
                            elide: Text.ElideRight
                            maximumLineCount: 2
                            wrapMode: Text.Wrap
                            verticalAlignment: Text.AlignVCenter
                            horizontalAlignment: isCurrent ? Text.AlignHCenter : Text.AlignLeft
                        }
                        Label {
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: 7
                            visible: isPrevious
                            textFormat: Text.PlainText
                            text: page.gradeMark(reviewedGrade)
                            color: page.gradeColor(reviewedGrade)
                            font.family: Theme.monoFont
                            font.pixelSize: 11
                        }
                    }
                    Rectangle {
                        z: -1
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        height: 1
                        color: Theme.rule
                        opacity: 0.68
                    }
                    Rectangle {
                        z: 1
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: Math.min(48, parent.width * 0.12)
                        color: Theme.surface
                        opacity: 0.92
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: Theme.surface }
                            GradientStop { position: 1.0; color: "transparent" }
                        }
                    }
                    Rectangle {
                        z: 1
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: Math.min(48, parent.width * 0.12)
                        color: Theme.surface
                        opacity: 0.92
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: "transparent" }
                            GradientStop { position: 1.0; color: Theme.surface }
                        }
                    }
                }
            }
            ScrollView {
                id: reviewModalScroll
                objectName: "reviewScroll"
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: reviewModalScroll.availableWidth
                    spacing: 18
                    Label {
                        textFormat: Text.PlainText
                        text: "Question"
                        color: Theme.inkMuted
                        font.family: Theme.monoFont
                        font.pixelSize: 11
                    }
                    MarkdownPane {
                        objectName: "reviewQuestion"
                        markdown: app.currentCard.question || app.currentCard.front || ""
                        mediaRoot: media.rootPath
                        Layout.fillWidth: true
                        Layout.preferredHeight: implicitHeight
                        baseFontSize: page.ui.width < 600 ? 20 : 24
                    }
                    Rectangle {
                        visible: app.answerRevealed
                        Layout.fillWidth: true
                        height: 1
                        color: Theme.rule
                    }
                    Label {
                        visible: app.answerRevealed
                        textFormat: Text.PlainText
                        text: "Answer"
                        color: Theme.inkMuted
                        font.family: Theme.monoFont
                        font.pixelSize: 11
                    }
                    MarkdownPane {
                        objectName: "reviewAnswer"
                        visible: app.answerRevealed
                        markdown: app.currentCard.answer || app.currentCard.back || ""
                        mediaRoot: media.rootPath
                        Layout.fillWidth: true
                        Layout.preferredHeight: implicitHeight
                        baseFontSize: page.ui.width < 600 ? 19 : 22
                    }
                    Label {
                        visible: app.spokenAnswer.length > 0
                        textFormat: Text.PlainText
                        text: "Your answer: " + app.spokenAnswer
                        color: Theme.inkMuted
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }
            }
            ColumnLayout {
                objectName: "reviewToolbar"
                Layout.fillWidth: true
                spacing: 10
                RowLayout {
                    visible: !app.answerRevealed
                    Layout.fillWidth: true
                    AppButton {
                        objectName: "revealAnswer"
                        text: "Reveal answer"
                        hint: shortcuts.bindings.review
                        Layout.fillWidth: true
                        enabled: !app.paused && !app.busy
                        onClicked: app.revealAnswer()
                    }
                }
                RowLayout {
                    visible: app.answerRevealed
                    Layout.fillWidth: true
                    spacing: page.ui.width < 600 ? 5 : 10
                    Repeater {
                        model: [
                            { grade: 0, label: "Missed", action: "gradeMissed" },
                            { grade: 1, label: "Partial", action: "gradePartial" },
                            { grade: 2, label: "Hard", action: "gradeHard" },
                            { grade: 3, label: "Good", action: "gradeGood" },
                            { grade: 4, label: "Easy", action: "gradeEasy" }
                        ]
                        delegate: AppButton {
                            required property var modelData
                            objectName: "grade" + modelData.grade
                            Layout.fillWidth: true
                            Layout.preferredHeight: 51
                            padding: 5
                            enabled: !app.paused && !app.busy
                            hint: modelData.label + " recall (" + shortcuts.bindings[modelData.action] + ")"
                            contentItem: Column {
                                spacing: 2
                                Label {
                                    textFormat: Text.PlainText
                                    width: parent.width
                                    text: shortcuts.bindings[modelData.action]
                                    horizontalAlignment: Text.AlignHCenter
                                    elide: Text.ElideRight
                                    font.family: Theme.monoFont
                                    font.pixelSize: 10
                                    color: Theme.inkMuted
                                }
                                Label {
                                    textFormat: Text.PlainText
                                    width: parent.width
                                    text: modelData.label
                                    horizontalAlignment: Text.AlignHCenter
                                    font.pixelSize: page.ui.width < 600 ? 12 : 14
                                    color: Theme.ink
                                }
                            }
                            onClicked: modelData.grade === 1 ? ui.openPartial() : app.grade(modelData.grade)
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    AppButton {
                        objectName: "deferCard"
                        text: page.ui.width < 600 ? "Queue end" : "Defer to queue end"
                        hint: shortcuts.bindings.defer
                        enabled: !app.paused && !app.busy
                        onClicked: app.deferCard()
                    }
                    AppButton {
                        objectName: "postponeCard"
                        text: "Later date"
                        hint: shortcuts.bindings.postpone
                        enabled: !app.paused && !app.busy
                        onClicked: ui.openPostpone()
                    }
                    Item { Layout.fillWidth: true }
                    GlyphButton {
                        objectName: "voiceToggle"
                        glyph: "mic"
                        selected: voice.enabled
                        hint: voice.enabled ? "Disable voice review" : "Enable voice review"
                        onClicked: voice.enabled = !voice.enabled
                    }
                }
                Label {
                    textFormat: Text.PlainText
                    visible: voice.enabled
                    text: voice.status
                    color: Theme.inkMuted
                    font.family: Theme.monoFont
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }
        }
    }
}
