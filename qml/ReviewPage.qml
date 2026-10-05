import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    required property var ui
    readonly property bool hasCard: !!app.currentCard.id
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
                    text: page.hasCard ? app.currentCard.deckName : (page.ui.selectedDeck.name || "Review")
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
            GlyphButton {
                objectName: "reviewMenuButton"
                glyph: "more"
                hint: "Review actions"
                onClicked: reviewMenu.open()
                Menu {
                    id: reviewMenu
                    y: parent.height
                    MenuItem {
                        text: "Choose deck"
                        enabled: !app.reviewing
                        onTriggered: page.ui.openDeckPicker()
                    }
                    MenuItem {
                        text: "Edit current card"
                        enabled: page.hasCard
                        onTriggered: page.ui.openCardEditor(app.currentCard.cardId || app.currentCard.id)
                    }
                    MenuItem {
                        text: "Summarize remaining cards"
                        enabled: app.reviewing && app.queueCount > 0
                        onTriggered: page.ui.openSummary()
                    }
                    MenuSeparator {
                        visible: app.reviewing
                    }
                    MenuItem {
                        text: "End review"
                        enabled: app.reviewing
                        onTriggered: app.stopReview()
                    }
                }
            }
            AppButton {
                objectName: "reviewPause"
                visible: app.reviewing
                text: app.paused ? "Resume" : "Pause"
                hint: shortcuts.bindings.pause
                primary: true
                enabled: !app.busy
                onClicked: app.paused ? app.resumeReview() : app.pauseReview()
            }
        }
        Item {
            id: idle
            objectName: "reviewIdle"
            visible: !page.hasCard
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
                            text: !app.decks.length ? "Create deck" : page.deckCount("cardCount") === 0 ? "Add card" : "Start review"
                            hint: "Review due cards in the selected deck and its subdecks"
                            primary: true
                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredWidth: Math.min(340, actionZone.width)
                            Layout.preferredHeight: page.ui.width < 600 ? 72 : 86
                            font.pixelSize: page.ui.width < 600 ? 21 : 25
                            enabled: !app.busy && (!app.decks.length || page.deckCount("cardCount") === 0 || page.deckCount("dueCount") > 0)
                            onClicked: {
                                if (!app.decks.length)
                                    page.ui.openDeckEditor("")
                                else if (page.deckCount("cardCount") === 0)
                                    page.ui.openCardEditor("")
                                else
                                    app.startReview(app.selectedDeckId)
                            }
                        }
                        Label {
                            textFormat: Text.PlainText
                            visible: app.decks.length > 0 && page.deckCount("cardCount") > 0 && page.deckCount("dueCount") === 0
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
        ScrollView {
            id: reviewScroll
            objectName: "reviewScroll"
            visible: page.hasCard
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: reviewScroll.availableWidth
                spacing: 22
                ColumnLayout {
                    visible: page.hasCard
                    Layout.fillWidth: true
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
                        textFormat: Text.PlainText
                        visible: app.answerRevealed
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
                        textFormat: Text.PlainText
                        visible: app.spokenAnswer.length > 0
                        text: "Your answer: " + app.spokenAnswer
                        color: Theme.inkMuted
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }
            }
        }
        ColumnLayout {
            objectName: "reviewToolbar"
            visible: page.hasCard && app.reviewing
            Layout.fillWidth: true
            spacing: 10
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Theme.rule
            }
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
                        onClicked: {
                            if (modelData.grade === 1)
                                page.ui.openPartial()
                            else
                                app.grade(modelData.grade)
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                AppButton {
                    objectName: "deferCard"
                    text: page.ui.width < 600 ? "Queue end" : "Defer to queue end"
                    hint: "Move this card to the end without grading (" + shortcuts.bindings.defer + ")"
                    enabled: !app.paused && !app.busy
                    onClicked: app.deferCard()
                }
                AppButton {
                    objectName: "postponeCard"
                    text: "Later date"
                    hint: "Choose a later review date (" + shortcuts.bindings.postpone + ")"
                    enabled: !app.paused && !app.busy
                    onClicked: page.ui.openPostpone()
                }
                Item {
                    Layout.fillWidth: true
                }
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
