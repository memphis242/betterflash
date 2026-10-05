import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    required property var ui
    readonly property bool hasCard: !!app.currentCard.id
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
                objectName: "reviewPrimary"
                text: app.reviewing ? (app.paused ? "Resume" : "Pause") : !app.decks.length ? "Create deck" : page.ui.selectedDeck.cardCount === 0 ? "Add card" : "Start review"
                hint: app.reviewing ? shortcuts.bindings.pause : "Review due cards in the selected deck"
                primary: true
                enabled: !app.busy
                onClicked: {
                    if (app.reviewing) {
                        if (app.paused)
                            app.resumeReview()
                        else
                            app.pauseReview()
                    } else if (!app.decks.length)
                        page.ui.openDeckEditor("")
                    else if (page.ui.selectedDeck.cardCount === 0)
                        page.ui.openCardEditor("")
                    else
                        app.startReview(app.selectedDeckId)
                }
            }
        }
        ScrollView {
            id: reviewScroll
            objectName: "reviewScroll"
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
                ColumnLayout {
                    visible: !page.hasCard
                    Layout.fillWidth: true
                    Layout.topMargin: page.ui.width < 600 ? 50 : 90
                    spacing: 14
                    Label {
                        textFormat: Text.PlainText
                        text: app.decks.length ? "Ready when you are" : "Make room for a new idea"
                        font.family: Theme.contentFont
                        font.pixelSize: page.ui.width < 600 ? 27 : 34
                        color: Theme.ink
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                    Label {
                        textFormat: Text.PlainText
                        text: app.decks.length ? (page.ui.selectedDeck.cardCount === 0 ? "Add the first card to this deck to begin a review." : "Start a review to study cards that are due" + (page.ui.selectedDeck.name ? " in " + page.ui.selectedDeck.name : "") + ".") : "Create a deck, then add a question and an answer in Markdown."
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
