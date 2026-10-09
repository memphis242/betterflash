import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml
import QtCore

Item {
    id: page
    required property var ui
    readonly property bool hasCard: !!app.currentCard.id
    readonly property bool correctionDialogVisible: gradeCorrectionDialog.visible
    readonly property bool previewHasFocus: reviewQueuePreviewList.activeFocus
    readonly property bool timelineScrollHasFocus: reviewQueueScrollBar.activeFocus
    readonly property bool reviewUtilityHasFocus: returnToActive.activeFocus || deferUtility.activeFocus || postponeUtility.activeFocus || voiceUtility.activeFocus
    property bool queueCollapsed: preferences.reviewQueueCollapsed
    property Settings preferences: Settings {
        category: "review"
        property bool reviewQueueCollapsed: false
    }
    onQueueCollapsedChanged: preferences.reviewQueueCollapsed = queueCollapsed
    readonly property var reviewCards: app.reviewCards
    readonly property var queueStats: [
        {
            key: "due",
            label: "Due now",
            count: deckCount("dueCount"),
            hint: "Review variants due in this deck and its subdecks"
        },
        {
            key: "new",
            label: "New due",
            count: deckCount("newDueCount"),
            hint: "Due variants that have never been reviewed"
        },
        {
            key: "review",
            label: "Reviewed due",
            count: deckCount("reviewDueCount"),
            hint: "Due variants with a previous review"
        },
        {
            key: "later",
            label: "Due later",
            count: deckCount("laterCount"),
            hint: "Variants with a future review date"
        }
    ]
    function deckCount(key) {
        if (ui.selectedDeck.id)
            return ui.selectedDeck[key] || 0;
        return app.decks.filter(deck => !deck.parentId).reduce((total, deck) => total + (deck[key] || 0), 0);
    }
    function cardTags(card) {
        const raw = card && (card.tags || card.labels || []);
        if (Array.isArray(raw))
            return raw.filter(tag => String(tag).trim().length > 0).map(tag => String(tag).trim());
        return String(raw || "").split(",").map(tag => tag.trim()).filter(tag => tag.length > 0);
    }
    function cardGrade(card) {
        if (!card)
            return -1;
        const grade = Number(card.sessionGrade);
        return Number.isFinite(grade) ? grade : -1;
    }
    function reviewSnippet(variant) {
        const prefix = "Ask the question that this answers based on deck context: \n\n";
        let question = variant.question || (variant.kind === "cloze" ? "" : variant.front || "");
        if (variant.variantKey === "reverse" && question.indexOf(prefix) === 0)
            question = question.slice(prefix.length);
        return ui.compactText(question) || "Image question";
    }
    function gradeColor(grade) {
        switch (grade) {
        case 0:
            return Theme.recallMissed;
        case 1:
            return Theme.recallPartial;
        case 2:
            return Theme.recallHard;
        case 3:
            return Theme.recallGood;
        case 4:
            return Theme.recallEasy;
        default:
            return Theme.rule;
        }
    }
    function gradeMark(grade) {
        switch (grade) {
        case 0:
            return "M";
        case 1:
            return "P";
        case 2:
            return "H";
        case 3:
            return "G";
        case 4:
            return "E";
        default:
            return "";
        }
    }
    function gradeName(grade) {
        switch (grade) {
        case 0:
            return "Missed";
        case 1:
            return "Partial";
        case 2:
            return "Hard";
        case 3:
            return "Good";
        case 4:
            return "Easy";
        default:
            return "";
        }
    }
    function formatElapsed(seconds) {
        const total = Math.max(0, Math.floor(Number(seconds) || 0));
        const minutes = Math.floor(total / 60);
        const remaining = total % 60;
        return (minutes < 10 ? "0" : "") + minutes + ":" + (remaining < 10 ? "0" : "") + remaining;
    }
    function reviewTimelineItem(index) {
        const cards = page.reviewCards || [];
        const card = cards[index];
        if (!card)
            return {
                kind: "empty",
                index: index
            };
        const grade = page.cardGrade(card);
        const current = card.variantId === app.currentCard.variantId;
        return {
            kind: current ? "current" : grade >= 0 ? "previous" : "upcoming",
            card: card,
            grade: grade,
            pendingIndex: index,
            upcomingIndex: index,
            index: index
        };
    }
    function recallLabel(fraction, points) {
        const count = Math.max(1, Number(points) || 1);
        const recalled = Math.max(0, Math.min(1, Number(fraction)));
        const amount = recalled * count;
        return count > 1 ? Number(amount.toFixed(2)) + " of " + count + " points" : Math.round(recalled * 100) + " of 100 parts";
    }
    function ratingDescription(grade, fraction) {
        return gradeName(Number(grade)) + (Number(grade) === 1 ? " (" + recallLabel(fraction, app.currentCard.pointCount) + ")" : "");
    }
    property string scrollVariantId: ""
    Connections {
        target: app
        function onPausedChanged() {
            if (app.reviewing && !app.paused)
                page.ui.page = 0;
        }
        function onCurrentCardChanged() {
            const id = app.currentCard.variantId || "";
            if (page.scrollVariantId !== id) {
                page.scrollVariantId = id;
                reviewContentScroll.contentItem.contentY = 0;
            }
        }
    }
    ColumnLayout {
        visible: !app.reviewing
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
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 20
                visible: !app.reviewing
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
                                    HoverHandler {
                                        id: statHover
                                    }
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
                            text: !app.decks.length ? "Create deck" : page.deckCount("cardCount") === 0 ? "Add card" : "Start review"
                            hint: "Review due cards in the selected deck and its subdecks"
                            primary: true
                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredWidth: Math.min(340, actionZone.width)
                            Layout.preferredHeight: page.ui.width < 600 ? 72 : 86
                            font.pixelSize: page.ui.width < 600 ? 21 : 25
                            enabled: !app.busy && (app.reviewing || !app.decks.length || page.deckCount("cardCount") === 0 || page.deckCount("dueCount") > 0)
                            onClicked: {
                                if (!app.decks.length)
                                    page.ui.openDeckEditor("");
                                else if (page.deckCount("cardCount") === 0)
                                    page.ui.openCardEditor("");
                                else
                                    app.startReview(app.selectedDeckId);
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
    Item {
        id: reviewWorkspace
        objectName: "reviewWorkspace"
        visible: app.reviewing
        anchors.fill: parent
        anchors.margins: page.ui.gutter
        ColumnLayout {
            anchors.fill: parent
            spacing: 14
            RowLayout {
                objectName: "reviewHeader"
                Layout.fillWidth: true
                spacing: 10
                ColumnLayout {
                    id: reviewHeaderInfo
                    readonly property real titleMaxWidth: Math.max(120, reviewWorkspace.width * 0.42)
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
                            primary: false
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
                Item {
                    Layout.fillWidth: true
                }
                AppButton {
                    visible: app.queueCount === 0
                    text: "Finish review"
                    primary: true
                    enabled: !app.busy
                    onClicked: app.stopReview()
                }
                GlyphButton {
                    objectName: "reviewMenuButton"
                    glyph: "more"
                    hint: "Review actions"
                    onClicked: reviewMenu.open()
                    Menu {
                        id: reviewMenu
                        y: parent.height
                        MenuItem {
                            text: "Edit current card"
                            onTriggered: ui.openCardEditor(app.currentCard.cardId || app.currentCard.id)
                        }
                        MenuItem {
                            text: "Summarize remaining cards"
                            enabled: app.queueCount > 0
                            onTriggered: ui.openSummary()
                        }
                        MenuItem {
                            text: app.reviewedCount >= app.sessionTotal ? "Finish review" : "End review"
                            onTriggered: app.stopReview()
                        }
                    }
                }
            }
            ReviewProgress {
                Layout.fillWidth: true
                cards: page.reviewCards
                reviewedCount: app.reviewedCount
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                Layout.minimumHeight: 44
                Layout.maximumHeight: 44
                Item {
                    Layout.fillWidth: true
                }
                ReviewUtilityButton {
                    id: returnToActive
                    objectName: "reviewQueueReturn"
                    visible: !page.queueCollapsed && !reviewQueuePreviewList.activeCardVisible && reviewQueuePreviewList.logicalActiveIndex >= 0
                    glyph: "return-card"
                    hint: "Center the active card in the review timeline"
                    onClicked: reviewQueuePreviewList.centerCurrent(true)
                }
                GlyphButton {
                    objectName: "reviewQueueCollapse"
                    visible: !page.queueCollapsed
                    glyph: "up"
                    hint: "Hide review queue"
                    onClicked: page.queueCollapsed = true
                }
                GlyphButton {
                    objectName: "reviewQueueReopen"
                    visible: page.queueCollapsed
                    glyph: "down"
                    hint: "Show review queue"
                    onClicked: page.queueCollapsed = false
                }
            }
            ColumnLayout {
                objectName: "reviewQueueBar"
                Layout.fillWidth: true
                spacing: 4
                visible: !page.queueCollapsed
                Flickable {
                    id: reviewQueuePreviewList
                    objectName: "reviewQueuePreview"
                    readonly property int count: page.reviewCards.length
                    readonly property int visibleStartIndex: Math.max(0, Math.min(count, Math.floor((contentX - leadingTrack) / stride) - 1))
                    readonly property int visibleEndIndex: Math.max(visibleStartIndex, Math.min(count, Math.ceil((contentX + width - leadingTrack) / stride) + 1))
                    readonly property int renderedCount: visibleEndIndex - visibleStartIndex
                    readonly property real slotWidth: Math.min(286, Math.max(0, width - 20))
                    readonly property real spacing: 10
                    readonly property real stride: slotWidth + spacing
                    readonly property real fadeWidth: Math.min(24, width * 0.08)
                    readonly property real leadingTrack: Math.max(0, (width - slotWidth) / 2)
                    readonly property int firstCardIndex: 0
                    readonly property real minimumScroll: Math.max(0, leadingTrack + firstCardIndex * stride + slotWidth / 2 - width / 2)
                    readonly property real maximumScroll: Math.max(minimumScroll, leadingTrack + (count - 1) * stride + slotWidth / 2 - width / 2)
                    property int currentIndex: Math.max(0, page.reviewCards.findIndex(card => card.variantId === app.currentCard.variantId))
                    readonly property int logicalActiveIndex: activeIndex()
                    readonly property string activeVariantId: app.currentCard.variantId || ""
                    readonly property real activeCardLeft: leadingTrack + logicalActiveIndex * stride
                    readonly property bool activeCardVisible: logicalActiveIndex >= 0 && activeCardLeft + slotWidth > contentX && activeCardLeft < contentX + width
                    readonly property real scrollSpan: maximumScroll - minimumScroll
                    visible: count > 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: 114
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
                        const bounded = Math.max(minimumScroll, Math.min(maximumScroll, contentX));
                        if (Math.abs(contentX - bounded) > 0.01)
                            contentX = bounded;
                    }
                    function activeIndex() {
                        const currentVariant = app.currentCard.variantId;
                        if (!currentVariant)
                            return -1;
                        return page.reviewCards.findIndex(card => card.variantId === currentVariant);
                    }
                    function centerCurrent(animate) {
                        centering.stop();
                        const index = logicalActiveIndex;
                        if (index < 0)
                            return;
                        cancelFlick();
                        currentIndex = index;
                        const target = Math.max(minimumScroll, Math.min(maximumScroll, leadingTrack + index * stride + slotWidth / 2 - width / 2));
                        if (animate && visible && !Theme.reducedMotion && Math.abs(contentX - target) > 1) {
                            centering.from = contentX;
                            centering.to = target;
                            centering.start();
                        } else {
                            contentX = target;
                        }
                    }
                    function centerAnimated() {
                        centerCurrent(true);
                    }
                    function centerImmediately() {
                        centerCurrent(false);
                    }
                    NumberAnimation {
                        id: centering
                        target: reviewQueuePreviewList
                        property: "contentX"
                        duration: 210
                        easing.type: Easing.OutCubic
                    }
                    WheelHandler {
                        target: null
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        onWheel: event => {
                            centering.stop();
                            event.accepted = false;
                        }
                    }
                    function selectVariant(variantId) {
                        if (!variantId || app.busy || app.paused || ui.modalOpen)
                            return;
                        app.selectReviewCard(variantId);
                        if (app.currentCard.variantId === variantId)
                            currentIndex = logicalActiveIndex;
                    }
                    function selectIndex(index) {
                        const item = page.reviewTimelineItem(index);
                        if (item.card)
                            selectVariant(item.card.variantId);
                    }
                    onLogicalActiveIndexChanged: Qt.callLater(centerAnimated)
                    onActiveVariantIdChanged: Qt.callLater(centerAnimated)
                    onDraggingChanged: {
                        if (dragging)
                            centering.stop();
                    }
                    onVisibleChanged: {
                        if (visible)
                            Qt.callLater(centerImmediately);
                        else
                            centering.stop();
                    }
                    onWidthChanged: Qt.callLater(centerImmediately)
                    onContentXChanged: clampScroll()
                    onMinimumScrollChanged: Qt.callLater(clampScroll)
                    onMaximumScrollChanged: Qt.callLater(clampScroll)
                    onCountChanged: {
                        currentIndex = Math.max(firstCardIndex, Math.min(count - 1, currentIndex));
                        Qt.callLater(clampScroll);
                    }
                    Component.onCompleted: Qt.callLater(centerImmediately)
                    Connections {
                        target: Theme
                        function onReducedMotionChanged() {
                            if (Theme.reducedMotion && centering.running)
                                reviewQueuePreviewList.centerImmediately();
                        }
                    }
                    Keys.onReturnPressed: selectIndex(currentIndex)
                    Keys.onEnterPressed: selectIndex(currentIndex)
                    Keys.onSpacePressed: selectIndex(currentIndex)
                    Row {
                        x: reviewQueuePreviewList.leadingTrack + reviewQueuePreviewList.visibleStartIndex * reviewQueuePreviewList.stride
                        height: reviewQueuePreviewList.height
                        spacing: reviewQueuePreviewList.spacing
                        Repeater {
                            model: reviewQueuePreviewList.renderedCount
                            delegate: Item {
                                id: timelineSlot
                                required property int index
                                readonly property int timelineIndex: reviewQueuePreviewList.visibleStartIndex + index
                                readonly property var modelData: page.reviewTimelineItem(timelineIndex)
                                readonly property var card: modelData.card || {}
                                readonly property string variantId: card.variantId || card.id || ""
                                readonly property bool isPrevious: modelData.kind === "previous"
                                readonly property bool isCurrent: variantId.length > 0 && variantId === app.currentCard.variantId
                                readonly property bool isUpcoming: modelData.kind === "upcoming"
                                readonly property int reviewedGrade: page.cardGrade(card)
                                readonly property bool isReviewed: reviewedGrade >= 0
                                readonly property int cardNumber: timelineIndex - reviewQueuePreviewList.firstCardIndex + 1
                                readonly property string ordinal: (cardNumber < 10 ? "0" : "") + cardNumber
                                readonly property real viewportLeft: reviewQueuePreviewList.leadingTrack + timelineIndex * reviewQueuePreviewList.stride - reviewQueuePreviewList.contentX
                                readonly property real revealAmount: isUpcoming && !isCurrent ? Math.max(0, Math.min(1, (reviewQueuePreviewList.width - reviewQueuePreviewList.fadeWidth - viewportLeft) / width)) : 1
                                objectName: isCurrent ? "reviewQueueCurrent" : isUpcoming ? (modelData.pendingIndex === 0 ? "reviewQueuePendingFirst" : "reviewQueueItem" + modelData.upcomingIndex) : "reviewTimelineItem" + timelineIndex
                                width: reviewQueuePreviewList.slotWidth
                                height: reviewQueuePreviewList.height
                                Rectangle {
                                    id: previewCard
                                    anchors.centerIn: parent
                                    width: parent.width
                                    height: 94
                                    visible: timelineSlot.modelData.kind !== "empty"
                                    opacity: timelineSlot.isPrevious && !timelineSlot.isCurrent ? 0.78 : timelineSlot.isCurrent ? 1 : 0.42 + 0.58 * timelineSlot.revealAmount
                                    color: timelineSlot.isCurrent ? Theme.ledgerSurfaceRaised : Theme.ledgerSurface
                                    border.color: timelineSlot.isCurrent ? Theme.accent : timelineSlot.isPrevious ? page.gradeColor(timelineSlot.reviewedGrade) : Theme.ledgerRule
                                    border.width: timelineSlot.isCurrent ? 3 : 1
                                    radius: 10
                                    ToolTip.visible: queueHover.hovered
                                    ToolTip.text: timelineSlot.isPrevious ? "Reviewed " + page.gradeName(timelineSlot.reviewedGrade) + ": " + page.reviewSnippet(timelineSlot.card) : page.reviewSnippet(timelineSlot.card)
                                    Accessible.role: Accessible.Button
                                    Accessible.name: timelineSlot.isPrevious ? "Reviewed " + page.gradeName(timelineSlot.reviewedGrade) + " card: " + page.reviewSnippet(timelineSlot.card) : timelineSlot.isCurrent ? "Current card: " + page.reviewSnippet(timelineSlot.card) : "Upcoming card: " + page.reviewSnippet(timelineSlot.card)
                                    Accessible.onPressAction: reviewQueuePreviewList.selectIndex(timelineSlot.timelineIndex)
                                    HoverHandler {
                                        id: queueHover
                                    }
                                    TapHandler {
                                        onTapped: {
                                            reviewQueuePreviewList.forceActiveFocus();
                                            reviewQueuePreviewList.selectIndex(timelineSlot.timelineIndex);
                                        }
                                    }
                                    Rectangle {
                                        x: 5
                                        y: 6
                                        width: 6
                                        height: parent.height - 12
                                        radius: 3
                                        color: timelineSlot.isReviewed ? page.gradeColor(timelineSlot.reviewedGrade) : Theme.ledgerRule
                                        opacity: timelineSlot.isCurrent ? 1 : 0.72
                                    }
                                    Label {
                                        x: 18
                                        y: 35
                                        textFormat: Text.PlainText
                                        text: timelineSlot.ordinal
                                        color: timelineSlot.isCurrent ? Theme.accent : Theme.inkMuted
                                        font.family: Theme.monoFont
                                        font.pixelSize: 16
                                        font.bold: true
                                        Accessible.name: "Review card " + timelineSlot.cardNumber + " of " + (reviewQueuePreviewList.count - reviewQueuePreviewList.firstCardIndex)
                                        ToolTip.visible: ordinalHover.hovered
                                        ToolTip.text: "Review card " + timelineSlot.cardNumber + " of " + (reviewQueuePreviewList.count - reviewQueuePreviewList.firstCardIndex)
                                        HoverHandler {
                                            id: ordinalHover
                                        }
                                    }
                                    Label {
                                        x: 58
                                        y: 12
                                        width: Math.max(0, parent.width - 74)
                                        textFormat: Text.PlainText
                                        text: (timelineSlot.card.deckName || page.ui.selectedDeck.name || "Review").toUpperCase()
                                        color: Theme.inkMuted
                                        font.family: Theme.monoFont
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                    }
                                    Label {
                                        x: 58
                                        y: 33
                                        width: Math.max(0, parent.width - 100)
                                        height: 34
                                        textFormat: Text.PlainText
                                        text: page.reviewSnippet(timelineSlot.card)
                                        color: Theme.ink
                                        font.family: Theme.monoFont
                                        font.pixelSize: 13
                                        elide: Text.ElideRight
                                        maximumLineCount: 2
                                        wrapMode: Text.Wrap
                                        verticalAlignment: Text.AlignTop
                                    }
                                    Row {
                                        x: 58
                                        y: parent.height - 25
                                        width: Math.max(0, parent.width - 94)
                                        height: 18
                                        spacing: 5
                                        Repeater {
                                            model: page.cardTags(timelineSlot.card).slice(0, 1)
                                            delegate: Rectangle {
                                                required property string modelData
                                                width: Math.min(120, tagLabel.implicitWidth + 12)
                                                height: 18
                                                radius: 9
                                                color: Theme.transparent
                                                border.color: Theme.ledgerRule
                                                Label {
                                                    id: tagLabel
                                                    anchors.centerIn: parent
                                                    text: modelData
                                                    width: parent.width - 10
                                                    color: Theme.inkMuted
                                                    font.family: Theme.monoFont
                                                    font.pixelSize: 9
                                                    elide: Text.ElideRight
                                                }
                                                ToolTip.visible: tagHover.hovered
                                                ToolTip.text: "Tag: " + modelData
                                                HoverHandler {
                                                    id: tagHover
                                                }
                                            }
                                        }
                                    }
                                    Rectangle {
                                        x: parent.width - 28
                                        y: 33
                                        width: 18
                                        height: 24
                                        radius: 3
                                        color: Theme.transparent
                                        border.width: timelineSlot.isCurrent ? 2 : 1
                                        border.color: timelineSlot.isReviewed ? page.gradeColor(timelineSlot.reviewedGrade) : timelineSlot.isCurrent ? Theme.accent : Theme.ledgerRule
                                        Accessible.name: timelineSlot.isReviewed ? "Review result: " + page.gradeName(timelineSlot.reviewedGrade) : "Not yet reviewed"
                                        ToolTip.visible: verdictHover.hovered
                                        ToolTip.text: timelineSlot.isReviewed ? "Review result: " + page.gradeName(timelineSlot.reviewedGrade) : "Not yet reviewed"
                                        HoverHandler {
                                            id: verdictHover
                                        }
                                        Label {
                                            anchors.centerIn: parent
                                            textFormat: Text.PlainText
                                            text: timelineSlot.isReviewed ? page.gradeMark(timelineSlot.reviewedGrade) : "·"
                                            color: timelineSlot.isReviewed ? page.gradeColor(timelineSlot.reviewedGrade) : Theme.inkMuted
                                            font.family: Theme.monoFont
                                            font.pixelSize: 11
                                        }
                                    }
                                    Rectangle {
                                        anchors.fill: parent
                                        anchors.margins: -3
                                        visible: reviewQueuePreviewList.activeFocus && reviewQueuePreviewList.currentIndex === timelineSlot.timelineIndex
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
                        z: -1
                        anchors.fill: parent
                        color: Theme.surface
                    }
                    Rectangle {
                        parent: reviewQueuePreviewList
                        z: 1
                        anchors.top: parent.top
                        width: parent.width
                        height: 1
                        color: Theme.ledgerRule
                    }
                    Rectangle {
                        parent: reviewQueuePreviewList
                        z: 1
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: Theme.ledgerRule
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
                            GradientStop {
                                position: 0.0
                                color: Theme.canvas
                            }
                            GradientStop {
                                position: 1.0
                                color: "transparent"
                            }
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
                            GradientStop {
                                position: 0.0
                                color: "transparent"
                            }
                            GradientStop {
                                position: 1.0
                                color: Theme.canvas
                            }
                        }
                    }
                }
                ScrollBar {
                    id: reviewQueueScrollBar
                    objectName: "reviewQueueScrollBar"
                    readonly property real scrollExtent: reviewQueuePreviewList.width + reviewQueuePreviewList.scrollSpan
                    Layout.fillWidth: true
                    Layout.preferredHeight: 18
                    orientation: Qt.Horizontal
                    policy: ScrollBar.AlwaysOn
                    active: true
                    interactive: reviewQueuePreviewList.scrollSpan > 0
                    activeFocusOnTab: interactive
                    hoverEnabled: true
                    topPadding: 6
                    bottomPadding: 6
                    size: scrollExtent > 0 ? reviewQueuePreviewList.width / scrollExtent : 1
                    position: scrollExtent > 0 ? (reviewQueuePreviewList.contentX - reviewQueuePreviewList.minimumScroll) / scrollExtent : 0
                    stepSize: scrollExtent > 0 ? reviewQueuePreviewList.stride / scrollExtent : 0
                    Accessible.role: Accessible.ScrollBar
                    Accessible.name: "Review timeline position"
                    ToolTip.visible: hovered
                    ToolTip.text: "Scroll through the review deck. Arrow keys move one card; Page Up and Page Down move one view; Home and End move to the first and last card."
                    function moveToContent(value) {
                        centering.stop();
                        reviewQueuePreviewList.cancelFlick();
                        reviewQueuePreviewList.contentX = Math.max(reviewQueuePreviewList.minimumScroll, Math.min(reviewQueuePreviewList.maximumScroll, value));
                    }
                    function applyPosition() {
                        if (interactive)
                            moveToContent(reviewQueuePreviewList.minimumScroll + position * scrollExtent);
                    }
                    onPositionChanged: {
                        if (pressed)
                            applyPosition();
                    }
                    onPressedChanged: {
                        if (pressed)
                            applyPosition();
                    }
                    Keys.onLeftPressed: moveToContent(reviewQueuePreviewList.contentX - reviewQueuePreviewList.stride)
                    Keys.onRightPressed: moveToContent(reviewQueuePreviewList.contentX + reviewQueuePreviewList.stride)
                    Keys.onPressed: event => {
                        switch (event.key) {
                        case Qt.Key_PageUp:
                            moveToContent(reviewQueuePreviewList.contentX - reviewQueuePreviewList.width);
                            break;
                        case Qt.Key_PageDown:
                            moveToContent(reviewQueuePreviewList.contentX + reviewQueuePreviewList.width);
                            break;
                        case Qt.Key_Home:
                            moveToContent(reviewQueuePreviewList.minimumScroll);
                            break;
                        case Qt.Key_End:
                            moveToContent(reviewQueuePreviewList.maximumScroll);
                            break;
                        default:
                            return;
                        }
                        event.accepted = true;
                    }
                    background: Rectangle {
                        anchors.centerIn: parent
                        width: parent.width
                        height: 2
                        radius: 1
                        color: Theme.rule
                    }
                    contentItem: Rectangle {
                        implicitHeight: 6
                        radius: 3
                        color: reviewQueueScrollBar.pressed || reviewQueueScrollBar.activeFocus ? Theme.accent : Theme.inkMuted
                    }
                }
            }
            Rectangle {
                id: reviewBody
                objectName: "reviewCardBody"
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.surface
                radius: 16
                border.color: Theme.rule
                border.width: 1
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8
                    ReviewNavigationBar {
                        objectName: "reviewPreviousCardBar"
                        Layout.preferredWidth: page.ui.width < 600 ? 28 : 38
                        Layout.fillHeight: true
                        direction: -1
                        hint: "Previous card (" + shortcuts.bindings.previousCard + ")"
                        enabled: reviewQueuePreviewList.logicalActiveIndex > 0 && !app.busy && !app.paused
                        onClicked: app.navigateReview(-1)
                    }
                    ColumnLayout {
                        id: reviewBodyCenter
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 10
                        ScrollView {
                            id: reviewContentScroll
                            objectName: "reviewScroll"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            contentWidth: availableWidth
                            clip: true
                            ColumnLayout {
                                width: reviewContentScroll.availableWidth
                                spacing: 18
                                ScrollView {
                                    id: questionTags
                                    visible: page.cardTags(app.currentCard).length > 0
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: Math.min(54, questionTagFlow.implicitHeight)
                                    contentWidth: availableWidth
                                    clip: true
                                    Flow {
                                        id: questionTagFlow
                                        width: questionTags.availableWidth
                                        spacing: 6
                                        Repeater {
                                            model: page.cardTags(app.currentCard)
                                            delegate: ReviewTag {
                                                required property string modelData
                                                label: modelData
                                                maximumWidth: Math.min(240, questionTags.availableWidth)
                                            }
                                        }
                                    }
                                }
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
                                readonly property bool running: !app.paused && app.reviewing && !app.answerRevealed
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
                                visible: app.answerRevealed
                                Layout.fillWidth: true
                                spacing: page.ui.width < 600 ? 5 : 10
                                Repeater {
                                    model: [
                                        {
                                            grade: 0,
                                            label: "Missed",
                                            action: "gradeMissed"
                                        },
                                        {
                                            grade: 1,
                                            label: "Partial",
                                            action: "gradePartial"
                                        },
                                        {
                                            grade: 2,
                                            label: "Hard",
                                            action: "gradeHard"
                                        },
                                        {
                                            grade: 3,
                                            label: "Good",
                                            action: "gradeGood"
                                        },
                                        {
                                            grade: 4,
                                            label: "Easy",
                                            action: "gradeEasy"
                                        }
                                    ]
                                    delegate: AppButton {
                                        required property var modelData
                                        readonly property bool selectedGrade: page.cardGrade(app.currentCard) === modelData.grade
                                        Accessible.checkable: true
                                        Accessible.checked: selectedGrade
                                        Accessible.name: modelData.label + (selectedGrade ? ", selected" : "")
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
                                        background: Rectangle {
                                            radius: 8
                                            color: parent.hovered || parent.down ? Theme.accentSoft : Theme.transparent
                                            border.width: selectedGrade ? 3 : parent.activeFocus ? 2 : 1
                                            border.color: selectedGrade ? page.gradeColor(modelData.grade) : parent.activeFocus ? Theme.accent : Theme.rule
                                        }
                                        onClicked: modelData.grade === 1 ? ui.openPartial() : app.grade(modelData.grade)
                                    }
                                }
                            }
                            AppButton {
                                objectName: "partialRecallAmount"
                                visible: app.answerRevealed && page.cardGrade(app.currentCard) === 1
                                text: page.recallLabel(app.currentCard.sessionRecall, app.currentCard.pointCount)
                                hint: "Adjust the amount recalled"
                                enabled: !app.paused && !app.busy
                                Layout.alignment: Qt.AlignHCenter
                                onClicked: ui.openPartial()
                            }
                            Item {
                                objectName: "reviewUtilityFooter"
                                Layout.fillWidth: true
                                Layout.preferredHeight: 48
                                Layout.minimumHeight: 48
                                Layout.maximumHeight: 48
                                Row {
                                    anchors.verticalCenter: parent.verticalCenter
                                    x: Math.max(0, Math.min((parent.width - width) / 2, parent.width - width - voiceUtility.width - 12))
                                    spacing: 12
                                    ReviewUtilityButton {
                                        id: deferUtility
                                        objectName: "deferCard"
                                        glyph: "queue-tail"
                                        hint: "Defer this card to the end of the review queue" + (shortcuts.bindings.defer ? " (" + shortcuts.bindings.defer + ")" : "")
                                        enabled: !app.paused && !app.busy && page.cardGrade(app.currentCard) < 0
                                        onClicked: app.deferCard()
                                    }
                                    ReviewUtilityButton {
                                        id: postponeUtility
                                        objectName: "postponeCard"
                                        glyph: "calendar-day"
                                        hint: "Review this card on a later date" + (shortcuts.bindings.postpone ? " (" + shortcuts.bindings.postpone + ")" : "")
                                        enabled: !app.paused && !app.busy && page.cardGrade(app.currentCard) < 0
                                        onClicked: ui.openPostpone()
                                    }
                                }
                                ReviewUtilityButton {
                                    id: voiceUtility
                                    objectName: "voiceToggle"
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
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
                    ReviewNavigationBar {
                        objectName: "reviewNextCardBar"
                        Layout.preferredWidth: page.ui.width < 600 ? 28 : 38
                        Layout.fillHeight: true
                        direction: 1
                        hint: "Next card (" + shortcuts.bindings.nextCard + ")"
                        enabled: reviewQueuePreviewList.logicalActiveIndex < page.reviewCards.length - 1 && !app.busy && !app.paused
                        onClicked: app.navigateReview(1)
                    }
                }
            }
        }
    }
    Dialog {
        id: gradeCorrectionDialog
        objectName: "gradeCorrectionDialog"
        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: Math.min(460, page.ui.width - 2 * page.ui.gutter)
        title: "Change rating"
        standardButtons: Dialog.Cancel
        closePolicy: Popup.CloseOnEscape
        visible: !!(app.pendingGradeCorrection && app.pendingGradeCorrection.variantId)
        onRejected: app.cancelGradeCorrection()
        background: Rectangle {
            color: Theme.surface
            radius: 12
            border.color: Theme.rule
            border.width: 1
        }
        contentItem: ColumnLayout {
            spacing: 14
            Label {
                text: {
                    const change = app.pendingGradeCorrection || {};
                    return "Change this card from " + page.ratingDescription(change.currentGrade, change.currentRecall) + " to " + page.ratingDescription(change.requestedGrade, change.requestedRecall) + "?";
                }
                color: Theme.ink
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Label {
                text: "The original review time and response duration stay the same. The schedule will use the corrected rating."
                color: Theme.inkMuted
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            AppButton {
                text: "Change rating"
                primary: true
                Layout.alignment: Qt.AlignRight
                enabled: !app.busy && !app.paused
                onClicked: app.confirmGradeCorrection()
            }
        }
    }
}
