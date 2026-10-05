import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml

Item {
    id: page
    required property var ui
    readonly property bool hasCard: !!app.currentCard.id
    readonly property bool reviewModalVisible: reviewDialog.visible
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
    Connections {
        target: app
        function onReviewingChanged() {
            if (app.reviewing)
                page.openReviewModal()
            else if (reviewDialog.visible)
                reviewDialog.close()
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
                spacing: 12
                AppButton {
                    objectName: "reviewPause"
                    text: app.paused ? "Resume" : "Pause"
                    hint: shortcuts.bindings.pause
                    primary: true
                    enabled: !app.busy
                    onClicked: app.paused ? app.resumeReview() : app.pauseReview()
                }
                ListView {
                    id: reviewQueuePreviewList
                    objectName: "reviewQueuePreview"
                    visible: count > 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: 60
                    clip: true
                    orientation: ListView.Horizontal
                    spacing: 8
                    model: app.pendingCards.slice(1, 6)
                    keyNavigationEnabled: true
                    activeFocusOnTab: true
                    cacheBuffer: 10000
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
                    Accessible.name: "Next review cards"
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        readonly property string variantId: modelData.variantId || modelData.id || ""
                        objectName: "reviewQueueItem" + index
                        width: Math.max(120, Math.min(190, (reviewQueuePreviewList.width - 32) / 5))
                        height: 54
                        ToolTip.visible: queueHover.hovered
                        ToolTip.text: page.reviewSnippet(modelData)
                        Accessible.name: page.reviewSnippet(modelData)
                        color: Theme.canvas
                        border.color: reviewQueuePreviewList.activeFocus && reviewQueuePreviewList.currentIndex === index ? Theme.accent : Theme.rule
                        border.width: reviewQueuePreviewList.activeFocus && reviewQueuePreviewList.currentIndex === index ? 2 : 1
                        radius: 5
                        HoverHandler { id: queueHover }
                        Label {
                            anchors.fill: parent
                            anchors.margins: 8
                            textFormat: Text.PlainText
                            text: page.reviewSnippet(modelData)
                            color: Theme.inkMuted
                            elide: Text.ElideRight
                            maximumLineCount: 2
                            wrapMode: Text.Wrap
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                    Keys.onLeftPressed: decrementCurrentIndex()
                    Keys.onRightPressed: incrementCurrentIndex()
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
