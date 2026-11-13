import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "ReviewPresentation.js" as ReviewPresentation

ColumnLayout {
    id: panel
    required property var ui
    property real viewportWidth: ui.width
    property bool showTags: true
    property bool showStudyActions: true
    readonly property bool hasCard: !!app.currentCard.id
    readonly property var weakness: app.currentCard.weakness || ({})
    readonly property var tags: ReviewPresentation.cardTags(app.currentCard)
    readonly property int resultGrade: ReviewPresentation.cardGrade(app.currentCard)
    readonly property color resultColor: ReviewPresentation.gradeColor(resultGrade, Theme)
    readonly property color weaknessColor: currentWeakness.tierColor
    signal generateRequested()
    signal studyHelpRequested()
    signal partialRequested()

    function cardTags(card) { return ReviewPresentation.cardTags(card) }
    function cardGrade(card) { return ReviewPresentation.cardGrade(card) }
    function gradeColor(grade) { return ReviewPresentation.gradeColor(grade, Theme) }
    function gradeHoverColor(grade) { return Theme.recallHoverFill(grade) }
    function formatElapsed(seconds) { return ReviewPresentation.formatElapsed(seconds) }
    function recallLabel(fraction, points) { return ReviewPresentation.recallLabel(fraction, points) }
    function resetScroll() { reviewContentScroll.contentItem.contentY = 0 }

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
                visible: panel.showTags && panel.cardTags(app.currentCard).length > 0
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(54, questionTagFlow.implicitHeight)
                contentWidth: availableWidth
                clip: true
                Flow {
                    id: questionTagFlow
                    width: questionTags.availableWidth
                    spacing: 6
                    Repeater {
                        model: panel.cardTags(app.currentCard)
                        delegate: ReviewTag {
                            required property string modelData
                            label: modelData
                            maximumWidth: Math.min(240, questionTags.availableWidth)
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Flow {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 12
                    Label {
                        textFormat: Text.PlainText
                        text: "Question"
                        color: Theme.inkMuted
                        font.family: Theme.monoFont
                        font.pixelSize: 15
                        height: currentWeakness.visible ? currentWeakness.implicitHeight : implicitHeight
                        verticalAlignment: Text.AlignVCenter
                    }
                    WeaknessBadge {
                        id: currentWeakness
                        assessment: panel.weakness
                        visible: !!panel.weakness.assessed
                    }
                }
                AppButton {
                    visible: panel.showStudyActions
                    text: panel.viewportWidth < 700 ? "Generate" : "Generate similar card"
                    glyph: "cards"
                    hint: "Draft a varied card testing the same concept. Review it before saving."
                    enabled: !app.busy && panel.hasCard
                    onClicked: panel.generateRequested()
                }
                AppButton {
                    visible: panel.showStudyActions
                    text: panel.viewportWidth < 700 ? "Recall & help" : "Recall & study help"
                    glyph: "book"
                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                    hint: "See this card's recall evidence, recall strength and suggested study steps. Ask the language model for help only if you choose."
                    enabled: !app.busy && panel.hasCard
                    onClicked: panel.studyHelpRequested()
                }
            }
            MarkdownPane {
                objectName: "reviewQuestion"
                markdown: app.currentCard.question || app.currentCard.front || ""
                mediaRoot: media.rootPath
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                baseFontSize: panel.viewportWidth < 600 ? 20 : 24
            }
            AppButton {
                id: revealAnswer
                objectName: "revealAnswer"
                visible: !app.answerRevealed
                text: "Reveal answer"
                hint: app.startupReviewActive ? "Reveal this answer. Q, Space, or Esc dismisses Review Pit Stop." : shortcuts.bindings.review
                primary: false
                Layout.fillWidth: true
                Layout.preferredHeight: panel.viewportWidth < 600 ? 148 : 188
                font.pixelSize: panel.viewportWidth < 600 ? 19 : 22
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
                font.pixelSize: 15
            }
            MarkdownPane {
                objectName: "reviewAnswer"
                visible: app.answerRevealed
                markdown: app.currentCard.answer || app.currentCard.back || ""
                mediaRoot: media.rootPath
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                baseFontSize: panel.viewportWidth < 600 ? 19 : 22
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
            text: panel.formatElapsed(app.responseSeconds)
            color: app.paused ? Theme.warning : Theme.inkMuted
            font.family: Theme.monoFont
            font.pixelSize: 18
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
        }
        RowLayout {
            visible: app.answerRevealed
            Layout.fillWidth: true
            spacing: panel.viewportWidth < 600 ? 5 : 10
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
                    id: gradeButton
                    required property var modelData
                    readonly property bool selectedGrade: panel.cardGrade(app.currentCard) === modelData.grade
                    Accessible.checkable: true
                    Accessible.checked: selectedGrade
                    Accessible.name: modelData.label + (selectedGrade ? ", selected" : "")
                    objectName: "grade" + modelData.grade
                    Layout.fillWidth: true
                    Layout.preferredHeight: panel.viewportWidth < 600 ? 60 : 68
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
                            font.pixelSize: 12
                            color: Theme.inkMuted
                        }
                        Label {
                            textFormat: Text.PlainText
                            width: parent.width
                            text: modelData.label
                            horizontalAlignment: Text.AlignHCenter
                            font.pixelSize: panel.viewportWidth < 600 ? 15 : 17
                            fontSizeMode: Text.Fit
                            minimumPixelSize: 12
                            color: Theme.ink
                        }
                    }
                    background: Rectangle {
                        radius: 8
                        color: gradeButton.hovered || gradeButton.down ? panel.gradeHoverColor(modelData.grade) : Theme.transparent
                        border.width: gradeButton.selectedGrade ? 3 : gradeButton.activeFocus ? 2 : 1
                        border.color: gradeButton.selectedGrade ? panel.gradeColor(modelData.grade) : gradeButton.activeFocus ? Theme.accent : Theme.rule
                    }
                    onClicked: modelData.grade === 1 ? panel.partialRequested() : app.grade(modelData.grade)
                }
            }
        }
        AppButton {
            objectName: "partialRecallAmount"
            visible: app.answerRevealed && panel.cardGrade(app.currentCard) === 1
            text: panel.recallLabel(app.currentCard.sessionRecall, app.currentCard.pointCount)
            hint: "Adjust the amount recalled. " + (app.currentCard.pointCount || 1) + " partial answer points: the number of points within this answer that can receive credit for partial grading."
            enabled: !app.paused && !app.busy
            Layout.alignment: Qt.AlignHCenter
            onClicked: panel.partialRequested()
        }
    }
}
