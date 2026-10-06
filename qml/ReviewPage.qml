import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml

Item {
    id: page
    required property var ui
    readonly property bool hasCard: !!app.currentCard.id
    readonly property bool reviewModalVisible: reviewDialog.visible
    readonly property bool previewHasFocus: reviewQueuePreviewList.activeFocus
    property var timelineCompleted: []
    property var timelineCurrentCard: ({})
    property string timelineCurrentVariant: ""
    property int timelineReviewedCount: 0
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
    function formatElapsed(seconds) {
        const total = Math.max(0, Math.floor(Number(seconds) || 0))
        const minutes = Math.floor(total / 60)
        const remaining = total % 60
        return (minutes < 10 ? "0" : "") + minutes + ":" + (remaining < 10 ? "0" : "") + remaining
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
                page.timelineReviewedCount = app.reviewedCount
                page.timelineCurrentCard = app.currentCard
                page.timelineCurrentVariant = app.currentCard.variantId || ""
                page.openReviewModal()
            } else if (reviewDialog.visible)
                reviewDialog.close()
        }
        function onCurrentCardChanged() {
            const nextVariant = app.currentCard.variantId || ""
            if (app.reviewedCount > page.timelineReviewedCount && page.timelineCurrentVariant && nextVariant !== page.timelineCurrentVariant) {
                const recorded = app.history.find(review => review.variantId === page.timelineCurrentVariant)
                if (recorded)
                    page.timelineCompleted = page.timelineCompleted.concat([{
                        card: page.timelineCurrentCard,
                        grade: recorded.grade
                    }]).slice(-2)
            }
            page.timelineReviewedCount = app.reviewedCount
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
                    id: reviewHeaderInfo
                    readonly property real titleMaxWidth: Math.max(120, reviewDialog.width * 0.42)
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: titleLabel.width + reviewPause.width + 10
                    spacing: 3
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Label {
                            id: titleLabel
                            objectName: "reviewDeckTitle"
                            textFormat: Text.PlainText
                            text: page.hasCard ? (app.currentCard.deckName || ui.selectedDeck.name || "Review") : "Review"
                            color: Theme.ink
                            font.family: Theme.contentFont
                            font.pixelSize: page.ui.width < 600 ? 21 : 27
                            elide: Text.ElideRight
                            Layout.minimumWidth: 0
                            Layout.preferredWidth: Math.min(reviewHeaderInfo.titleMaxWidth, implicitWidth)
                            Layout.maximumWidth: reviewHeaderInfo.titleMaxWidth
                        }
                        AppButton {
                            id: reviewPause
                            objectName: "reviewPause"
                            text: app.paused ? "Resume" : "Pause"
                            hint: shortcuts.bindings.pause
                            primary: true
                            enabled: !app.busy
                            onClicked: app.paused ? app.resumeReview() : app.pauseReview()
                        }
                    }
                    Label {
                        textFormat: Text.PlainText
                        text: app.reviewedCount + " of " + app.sessionTotal + " reviewed" + (app.paused ? " - paused" : "")
                        color: app.paused ? Theme.warning : Theme.inkMuted
                        font.family: Theme.monoFont
                        font.pixelSize: 11
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                }
                Item { Layout.fillWidth: true }
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
                Flickable {
                    id: reviewQueuePreviewList
                    objectName: "reviewQueuePreview"
                    readonly property bool timelineMode: true
                    readonly property bool scrollable: interactive
                    readonly property bool horizontalScrollable: true
                    readonly property bool autoCentersCurrent: false
                    readonly property bool railVisible: false
                    readonly property bool currentLabelVisible: false
                    readonly property int previousCount: page.timelineCompleted.length
                    readonly property int upcomingCount: Math.min(5, Math.max(0, app.pendingCards.length - 1))
                    readonly property bool previousSpaceReserved: true
                    readonly property bool fadeEnds: true
                    readonly property bool previousCardsFaded: true
                    readonly property bool upcomingCardsHazy: true
                    readonly property bool previousOutlines: true
                    readonly property var timelineItems: page.reviewTimelineItems()
                    readonly property int count: timelineItems.length
                    readonly property real slotWidth: Math.max(92, Math.min(158, (width - 32) / 5))
                    readonly property real spacing: 8
                    readonly property real stride: slotWidth + spacing
                    readonly property real fadeWidth: Math.min(32, width * 0.08)
                    readonly property real leadingTrack: Math.max(0, (width - slotWidth) / 2)
                    readonly property int firstCardIndex: timelineItems.findIndex(item => item.kind !== "empty")
                    readonly property real minimumScroll: firstCardIndex < 0 ? 0 : Math.max(0, leadingTrack + firstCardIndex * stride + slotWidth / 2 - width / 2)
                    readonly property real maximumScroll: Math.max(minimumScroll, leadingTrack + (count - 1) * stride + slotWidth + fadeWidth - width)
                    property int currentIndex: 2
                    property string selectedVariantToCenter: ""
                    visible: count > 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: page.ui.width < 600 ? 82 : 92
                    clip: true
                    interactive: true
                    activeFocusOnTab: true
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    boundsMovement: Flickable.StopAtBounds
                    contentWidth: width + maximumScroll
                    contentHeight: height
                    Accessible.name: "Upcoming review timeline"
                    function clampScroll() {
                        const bounded = Math.max(minimumScroll, Math.min(maximumScroll, contentX))
                        if (Math.abs(contentX - bounded) > 0.01)
                            contentX = bounded
                    }
                    function activeIndex() {
                        return timelineItems.findIndex(item => item.card && item.card.variantId === app.currentCard.variantId)
                    }
                    function centerCurrent() {
                        const index = activeIndex()
                        if (index < 0)
                            return
                        currentIndex = index
                        contentX = Math.max(minimumScroll, Math.min(maximumScroll, leadingTrack + index * stride + slotWidth / 2 - width / 2))
                    }
                    function focusIndex(index) {
                        currentIndex = index
                        const left = leadingTrack + index * stride
                        const right = left + slotWidth
                        if (left < contentX + fadeWidth)
                            contentX = Math.max(minimumScroll, left - fadeWidth)
                        else if (right > contentX + width - fadeWidth)
                            contentX = Math.min(maximumScroll, right + fadeWidth - width)
                    }
                    function decrementCurrentIndex() {
                        focusIndex(Math.max(firstCardIndex, currentIndex - 1))
                    }
                    function incrementCurrentIndex() {
                        focusIndex(Math.min(count - 1, Math.max(firstCardIndex, currentIndex + 1)))
                    }
                    function selectVariant(variantId) {
                        if (!variantId || app.busy)
                            return
                        selectedVariantToCenter = variantId
                        app.selectReviewCard(variantId)
                        if (app.currentCard.variantId === variantId) {
                            selectedVariantToCenter = ""
                            Qt.callLater(centerCurrent)
                        }
                    }
                    function selectIndex(index) {
                        const item = timelineItems[index]
                        if (item && item.card)
                            selectVariant(item.card.variantId)
                    }
                    onContentXChanged: clampScroll()
                    onMinimumScrollChanged: Qt.callLater(clampScroll)
                    onMaximumScrollChanged: Qt.callLater(clampScroll)
                    onTimelineItemsChanged: {
                        currentIndex = Math.max(firstCardIndex, Math.min(count - 1, currentIndex))
                        Qt.callLater(clampScroll)
                    }
                    Component.onCompleted: Qt.callLater(centerCurrent)
                    Connections {
                        target: reviewDialog
                        function onOpened() {
                            Qt.callLater(reviewQueuePreviewList.centerCurrent)
                        }
                    }
                    Connections {
                        target: app
                        function onCurrentCardChanged() {
                            if (reviewQueuePreviewList.selectedVariantToCenter === app.currentCard.variantId) {
                                reviewQueuePreviewList.selectedVariantToCenter = ""
                                Qt.callLater(reviewQueuePreviewList.centerCurrent)
                            }
                        }
                        function onBusyChanged() {
                            if (!app.busy)
                                Qt.callLater(function() {
                                    if (!app.busy && reviewQueuePreviewList.selectedVariantToCenter !== app.currentCard.variantId)
                                        reviewQueuePreviewList.selectedVariantToCenter = ""
                                })
                        }
                    }
                    Keys.onLeftPressed: decrementCurrentIndex()
                    Keys.onRightPressed: incrementCurrentIndex()
                    Keys.onReturnPressed: selectIndex(currentIndex)
                    Keys.onEnterPressed: selectIndex(currentIndex)
                    Keys.onSpacePressed: selectIndex(currentIndex)
                    Row {
                        x: reviewQueuePreviewList.leadingTrack
                        height: reviewQueuePreviewList.height
                        spacing: reviewQueuePreviewList.spacing
                        Repeater {
                            model: reviewQueuePreviewList.timelineItems
                            delegate: Item {
                                id: timelineSlot
                                required property var modelData
                                required property int index
                                readonly property var card: modelData.card || {}
                                readonly property string variantId: card.variantId || card.id || ""
                                readonly property bool isPrevious: modelData.kind === "previous"
                                readonly property bool isCurrent: variantId.length > 0 && variantId === app.currentCard.variantId
                                readonly property bool isUpcoming: modelData.kind === "upcoming"
                                readonly property bool currentLabelVisible: false
                                readonly property int reviewedGrade: isPrevious ? Number(modelData.grade) : -1
                                readonly property bool reviewOutline: isPrevious
                                readonly property string timelineRole: modelData.kind
                                readonly property real renderedOpacity: previewCard.opacity
                                readonly property real viewportLeft: reviewQueuePreviewList.leadingTrack + index * reviewQueuePreviewList.stride - reviewQueuePreviewList.contentX
                                readonly property real revealAmount: isUpcoming && !isCurrent ? Math.max(0, Math.min(1, (reviewQueuePreviewList.width - reviewQueuePreviewList.fadeWidth - viewportLeft) / width)) : 1
                                objectName: isCurrent ? "reviewQueueCurrent" : isUpcoming ? "reviewQueueItem" + (modelData.index - 3) : "reviewTimelineItem" + index
                                width: reviewQueuePreviewList.slotWidth
                                height: reviewQueuePreviewList.height
                                Rectangle {
                                    id: previewCard
                                    anchors.centerIn: parent
                                    width: parent.width * (0.38 + 0.62 * timelineSlot.revealAmount)
                                    height: timelineSlot.isCurrent ? 72 : 66
                                    visible: timelineSlot.modelData.kind !== "empty"
                                    opacity: timelineSlot.isPrevious && !timelineSlot.isCurrent ? 0.52 : timelineSlot.isCurrent ? 1 : 0.25 + 0.75 * timelineSlot.revealAmount
                                    color: timelineSlot.isCurrent ? Theme.surfaceRaised : Theme.canvas
                                    border.color: timelineSlot.isPrevious ? page.gradeColor(timelineSlot.reviewedGrade) : timelineSlot.isCurrent ? Theme.accent : Theme.rule
                                    border.width: timelineSlot.isCurrent ? 2 : 1
                                    radius: timelineSlot.isCurrent ? 7 : 5
                                    ToolTip.visible: queueHover.hovered
                                    ToolTip.text: timelineSlot.isPrevious ? "Reviewed " + page.gradeName(timelineSlot.reviewedGrade) + ": " + page.reviewSnippet(timelineSlot.card) : page.reviewSnippet(timelineSlot.card)
                                    Accessible.role: Accessible.Button
                                    Accessible.name: timelineSlot.isPrevious ? "Reviewed " + page.gradeName(timelineSlot.reviewedGrade) + " card: " + page.reviewSnippet(timelineSlot.card) : timelineSlot.isCurrent ? "Current card: " + page.reviewSnippet(timelineSlot.card) : "Upcoming card: " + page.reviewSnippet(timelineSlot.card)
                                    Accessible.onPressAction: reviewQueuePreviewList.selectIndex(timelineSlot.index)
                                    HoverHandler { id: queueHover }
                                    TapHandler {
                                        onTapped: {
                                            reviewQueuePreviewList.forceActiveFocus()
                                            reviewQueuePreviewList.selectIndex(timelineSlot.index)
                                        }
                                    }
                                    Label {
                                        anchors.fill: parent
                                        anchors.margins: 8
                                        anchors.rightMargin: timelineSlot.isPrevious ? 18 : 8
                                        textFormat: Text.PlainText
                                        text: page.reviewSnippet(timelineSlot.card)
                                        color: timelineSlot.isCurrent ? Theme.ink : Theme.inkMuted
                                        elide: Text.ElideRight
                                        maximumLineCount: timelineSlot.revealAmount > 0.65 ? 3 : 2
                                        wrapMode: Text.Wrap
                                        verticalAlignment: Text.AlignVCenter
                                        horizontalAlignment: timelineSlot.isCurrent ? Text.AlignHCenter : Text.AlignLeft
                                    }
                                    Label {
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        anchors.margins: 7
                                        visible: timelineSlot.isPrevious
                                        textFormat: Text.PlainText
                                        text: page.gradeMark(timelineSlot.reviewedGrade)
                                        color: page.gradeColor(timelineSlot.reviewedGrade)
                                        font.family: Theme.monoFont
                                        font.pixelSize: 11
                                    }
                                    Rectangle {
                                        anchors.fill: parent
                                        anchors.margins: -3
                                        visible: reviewQueuePreviewList.activeFocus && reviewQueuePreviewList.currentIndex === timelineSlot.index
                                        color: "transparent"
                                        border.color: Theme.inkMuted
                                        border.width: 1
                                        radius: previewCard.radius + 3
                                    }
                                }
                            }
                        }
                    }
                    Rectangle {
                        parent: reviewQueuePreviewList
                        z: 1
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: reviewQueuePreviewList.fadeWidth
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0.0; color: Theme.surface }
                            GradientStop { position: 1.0; color: "transparent" }
                        }
                    }
                    Rectangle {
                        parent: reviewQueuePreviewList
                        z: 1
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: reviewQueuePreviewList.fadeWidth
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
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
                    AppButton {
                        id: revealAnswer
                        objectName: "revealAnswer"
                        visible: !app.answerRevealed
                        text: "Reveal answer"
                        hint: shortcuts.bindings.review
                        primary: false
                        Layout.fillWidth: true
                        Layout.preferredHeight: page.ui.width < 600 ? 148 : 188
                        font.pixelSize: page.ui.width < 600 ? 19 : 22
                        enabled: !app.paused && !app.busy
                        property real radius: 14
                        Accessible.name: "Reveal answer"
                        background: Rectangle {
                            radius: revealAnswer.radius
                            color: revealAnswer.enabled ? (revealAnswer.hovered ? Theme.accentSoft : Theme.surfaceRaised) : Theme.canvas
                            border.width: revealAnswer.activeFocus ? 2 : 1
                            border.color: revealAnswer.activeFocus ? Theme.accent : Theme.rule
                            opacity: revealAnswer.enabled ? 1 : 0.7
                        }
                        onClicked: app.revealAnswer()
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
                Label {
                    objectName: "reviewTimer"
                    readonly property bool running: !app.paused && app.reviewing && !app.reviewingCompletedCard
                    readonly property real elapsedSeconds: app.responseSeconds
                    textFormat: Text.PlainText
                    text: page.formatElapsed(app.responseSeconds)
                    color: app.paused ? Theme.warning : Theme.inkMuted
                    font.family: Theme.monoFont
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    Layout.fillWidth: true
                }
                RowLayout {
                    visible: app.answerRevealed && !app.reviewingCompletedCard
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
                AppButton {
                    objectName: "returnToReview"
                    visible: app.reviewingCompletedCard
                    text: "Return to review"
                    primary: true
                    enabled: app.pendingCards.length > 0 && !app.busy
                    Layout.alignment: Qt.AlignHCenter
                    onClicked: reviewQueuePreviewList.selectVariant(app.pendingCards[0].variantId)
                }
                RowLayout {
                    visible: !app.reviewingCompletedCard
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
