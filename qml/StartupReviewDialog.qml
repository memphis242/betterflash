import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Layouts
import QtQml
import "ReviewPresentation.js" as ReviewPresentation

Dialog {
    id: dialog
    objectName: "startupReviewDialog"
    required property var ui
    property bool startRequested: false
    property bool finishing: false
    property bool backgroundPaused: false
    property bool toolPaused: false
    property int resultGrade: -1
    readonly property bool inReview: app.startupReviewActive && app.reviewing
        && app.currentCard.variantId === app.startupReviewCard.variantId
    readonly property bool unavailable: app.startupReviewActive && !inReview && !app.busy
    readonly property var card: inReview ? app.currentCard : app.startupReviewCard
    readonly property var tags: ReviewPresentation.cardTags(card)
    readonly property color rimColor: resultGrade >= 0
        ? ReviewPresentation.gradeColor(resultGrade, Theme) : Theme.ledgerRule
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(1120, ui.width - 2 * ui.gutter)
    height: Math.min(900, ui.height - 2 * ui.gutter)
    padding: width < 600 ? 14 : 24
    title: "Review Pit Stop"
    modal: true
    focus: true
    closePolicy: Popup.NoAutoClose
    background: Rectangle {
        radius: 16
        color: Theme.surface
        border.color: dialog.rimColor
        border.width: dialog.resultGrade >= 0 ? 5 : 1
        layer.enabled: dialog.visible && dialog.resultGrade >= 0
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: dialog.rimColor
            shadowOpacity: 0.22
            shadowBlur: 0.5
            blurMax: 16
            shadowScale: 1.0
            shadowHorizontalOffset: 0
            shadowVerticalOffset: 0
        }
    }
    Overlay.modal: Rectangle {
        color: Theme.scrim
        MouseArea {
            anchors.fill: parent
            enabled: !app.busy && !dialog.ui.pitStopToolOpen
            onClicked: dialog.requestClose()
        }
    }
    header: Item {
        implicitHeight: heading.implicitHeight + 24
        ColumnLayout {
            id: heading
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: dialog.padding
            anchors.rightMargin: dialog.padding
            anchors.top: parent.top
            anchors.topMargin: 12
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Label {
                    text: dialog.title
                    color: Theme.ink
                    font.family: Theme.contentFont
                    font.pixelSize: dialog.width < 600 ? 21 : 27
                    font.bold: true
                    elide: Text.ElideRight
                    Layout.minimumWidth: 0
                }
                GlyphButton {
                    id: pitStopHelp
                    glyph: "help"
                    hint: "Review Pit Stop offers one short due card at startup. Disable it in Settings > Review."
                    Accessible.name: "About Review Pit Stop"
                    toolTipOnFocus: true
                }
                Item { Layout.fillWidth: true }
                ReviewUtilityButton {
                    id: pitStopPause
                    objectName: "pitStopPause"
                    glyph: app.paused ? "play" : "pause"
                    hint: (app.paused ? "Resume review" : "Pause review") + " (" + shortcuts.bindings.pause + ")"
                    enabled: dialog.inReview && !app.busy && !dialog.ui.pitStopToolOpen
                    onClicked: dialog.togglePause()
                }
                GlyphButton {
                    id: pitStopClose
                    glyph: "close"
                    hint: "Close Review Pit Stop"
                    enabled: !app.busy && !dialog.ui.pitStopToolOpen
                    onClicked: dialog.requestClose()
                }
            }
            GridLayout {
                columns: dialog.width < 760 ? 1 : 2
                columnSpacing: 16
                rowSpacing: 8
                Layout.fillWidth: true
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 7
                    Label {
                        text: dialog.card.deckName || ""
                        textFormat: Text.PlainText
                        color: Theme.inkMuted
                        font.pixelSize: 15
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                    ScrollView {
                        id: tagViewport
                        visible: dialog.tags.length > 0
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(54, tagFlow.implicitHeight)
                        contentWidth: availableWidth
                        clip: true
                        Flow {
                            id: tagFlow
                            width: tagViewport.availableWidth
                            spacing: 6
                            Repeater {
                                model: dialog.tags
                                delegate: ReviewTag {
                                    required property string modelData
                                    label: modelData
                                    maximumWidth: Math.min(240, tagViewport.availableWidth)
                                }
                            }
                        }
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    Layout.preferredWidth: implicitWidth
                    Layout.alignment: Qt.AlignRight | Qt.AlignTop
                    spacing: 12
                    AppButton {
                        id: pitStopGenerate
                        text: dialog.width < 600 ? "Generate" : "Generate similar card"
                        glyph: "cards"
                        hint: "Draft a varied card testing the same concept. Review it before saving."
                        enabled: dialog.inReview && !app.busy
                        onClicked: dialog.openGeneration()
                    }
                    AppButton {
                        id: pitStopStudy
                        text: dialog.width < 600 ? "Recall & help" : "Recall & study help"
                        glyph: "book"
                        hint: "See this card's recall evidence, recall strength and suggested study steps."
                        enabled: dialog.inReview && !app.busy
                        onClicked: dialog.openStudyHelp()
                    }
                }
            }
        }
    }

    function startWhenReady() {
        if (!visible || finishing || startRequested || app.startupReviewActive
            || app.busy || !app.startupReviewCard.variantId)
            return
        startRequested = true
        app.startStartupReview()
    }
    function requestClose() {
        if (app.busy || ui.pitStopToolOpen)
            return
        finishing = true
        app.dismissStartupReview()
        if (!app.startupReviewActive)
            close()
    }
    function finishCloseIfReady() {
        if (visible && !app.busy && finishing && !app.startupReviewActive)
            close()
    }
    function syncPausedState() {
        if (!visible || !app.startupReviewActive || !app.reviewing || app.busy || finishing)
            return
        if (ui.pitStopToolOpen) {
            if (!app.answerRevealed && !app.paused) {
                toolPaused = true
                app.pauseReview()
            }
            return
        }
        if (!ui.active) {
            if (!app.answerRevealed && !app.paused) {
                backgroundPaused = true
                app.pauseReview()
            }
            return
        }
        if ((backgroundPaused || toolPaused) && app.paused) {
            backgroundPaused = false
            toolPaused = false
            app.resumeReview()
        }
    }
    function togglePause() {
        backgroundPaused = false
        toolPaused = false
        if (app.paused) app.resumeReview()
        else app.pauseReview()
    }
    function pauseForTool() {
        if (!app.paused) {
            toolPaused = true
            app.pauseReview()
        }
    }
    function openGeneration() {
        if (!inReview || app.busy) return
        pauseForTool()
        ui.openGenerateSimilar(card.cardId || card.id)
        Qt.callLater(dialog.syncPausedState)
    }
    function openStudyHelp() {
        if (!inReview || app.busy) return
        pauseForTool()
        ui.openStudyGuidance(card.cardId || card.id, card.variantId)
        Qt.callLater(dialog.syncPausedState)
    }
    function updateResult() {
        if (inReview)
            resultGrade = ReviewPresentation.cardGrade(app.currentCard)
    }
    onOpened: {
        startRequested = false
        finishing = false
        backgroundPaused = false
        toolPaused = false
        resultGrade = -1
        Qt.callLater(dialog.startWhenReady)
    }
    onClosed: {
        if (!app.busy && (app.startupReviewActive || app.startupReviewCard.variantId))
            app.dismissStartupReview()
    }
    Connections {
        target: app
        function onBusyChanged() {
            Qt.callLater(dialog.startWhenReady)
            Qt.callLater(dialog.syncPausedState)
            Qt.callLater(dialog.finishCloseIfReady)
            Qt.callLater(dialog.updateResult)
        }
        function onReviewingChanged() { Qt.callLater(dialog.syncPausedState) }
        function onReviewStateChanged() { dialog.updateResult() }
        function onCurrentCardChanged() { dialog.updateResult() }
        function onAnswerRevealedChanged() {
            if (dialog.visible && dialog.inReview && reviewLoader.item) reviewLoader.item.resetScroll()
        }
        function onStartupReviewActiveChanged() {
            if (!app.startupReviewActive && dialog.finishing) dialog.close()
        }
        function onStartupReviewCardChanged() {
            if (dialog.visible && !app.startupReviewActive && !app.startupReviewCard.variantId)
                dialog.close()
        }
        function onLastErrorChanged() {
            if (dialog.visible && app.lastError.message)
                dialog.finishing = false
        }
    }
    Connections {
        target: dialog.ui
        function onActiveChanged() { dialog.syncPausedState() }
        function onPitStopToolOpenChanged() { Qt.callLater(dialog.syncPausedState) }
    }
    contentItem: ColumnLayout {
        spacing: 12
        Shortcut {
            sequence: "Escape"
            enabled: dialog.visible && dialog.activeFocus && !app.busy && !dialog.ui.pitStopToolOpen
            autoRepeat: false
            onActivated: dialog.requestClose()
        }
        Instantiator {
            model: ["review", "gradeMissed", "gradePartial", "gradeHard", "gradeGood", "gradeEasy", "pause"]
            delegate: Shortcut {
                required property string modelData
                sequence: shortcuts.bindings[modelData] || ""
                autoRepeat: false
                enabled: dialog.visible && dialog.activeFocus && dialog.inReview && !app.busy
                    && !dialog.ui.pitStopToolOpen && (!app.paused || modelData === "pause")
                    && (modelData !== "review" || !(pitStopPause.activeFocus || pitStopClose.activeFocus
                        || pitStopHelp.activeFocus || pitStopGenerate.activeFocus || pitStopStudy.activeFocus || leaveButton.activeFocus))
                onActivated: {
                    switch (modelData) {
                    case "review": if (!app.answerRevealed) app.revealAnswer(); break
                    case "pause": dialog.togglePause(); break
                    case "gradePartial": if (app.answerRevealed) dialog.ui.openPartial(); break
                    case "gradeMissed": if (app.answerRevealed) app.grade(0); break
                    case "gradeHard": if (app.answerRevealed) app.grade(2); break
                    case "gradeGood": if (app.answerRevealed) app.grade(3); break
                    case "gradeEasy": if (app.answerRevealed) app.grade(4); break
                    }
                }
            }
        }
        Label {
            visible: !dialog.inReview && !dialog.unavailable
            text: "Opening Review Pit Stop..."
            color: Theme.inkMuted
            Layout.fillWidth: true
        }
        ErrorBlock {
            visible: dialog.unavailable
            Layout.fillWidth: true
            code: "STARTUP_REVIEW_STALE"
            message: "The selected card is no longer available in this review queue. Close this prompt and start a regular review."
            details: "Review variant: " + (app.startupReviewCard.variantId || "unavailable")
        }
        Loader {
            id: reviewLoader
            active: dialog.visible && dialog.inReview
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            sourceComponent: ReviewCardPanel {
                ui: dialog.ui
                viewportWidth: dialog.width
                showTags: false
                showStudyActions: false
                onGenerateRequested: dialog.openGeneration()
                onStudyHelpRequested: dialog.openStudyHelp()
                onPartialRequested: dialog.ui.openPartial()
            }
        }
        Item { visible: !dialog.inReview; Layout.fillHeight: true }
    }
    footer: RowLayout {
        spacing: 12
        Label {
            visible: dialog.finishing
            text: "Closing Review Pit Stop..."
            color: Theme.inkMuted
            Layout.fillWidth: true
        }
        Item { Layout.fillWidth: true }
        AppButton {
            id: leaveButton
            text: dialog.resultGrade >= 0 ? "Close" : "Leave for later"
            enabled: !app.busy && !dialog.ui.pitStopToolOpen
            onClicked: dialog.requestClose()
        }
    }
}
