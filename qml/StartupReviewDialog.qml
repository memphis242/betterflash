import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "startupReviewDialog"
    required property var ui
    property bool startRequested: false
    property bool finishing: false
    property bool completionHandled: false
    readonly property bool inReview: app.startupReviewActive && app.reviewing
        && app.currentCard.variantId === app.startupReviewCard.variantId
    readonly property bool unavailable: app.startupReviewActive && !inReview && !app.busy
    readonly property var card: inReview ? app.currentCard : app.startupReviewCard
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(800, ui.width - 2 * ui.gutter)
    height: Math.min(inReview || startRequested ? 680 : 300, ui.height - 2 * ui.gutter)
    padding: width < 600 ? 14 : 24
    modal: true
    focus: true
    closePolicy: Popup.NoAutoClose
    background: Rectangle { radius: 16; color: Theme.surface; border.color: Theme.ledgerRule }
    Overlay.modal: Rectangle {
        color: Theme.scrim
        MouseArea {
            anchors.fill: parent
            enabled: !app.busy
            onClicked: dialog.requestClose()
        }
    }
    header: Item {
        implicitHeight: 64
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: dialog.padding
            anchors.rightMargin: dialog.padding
            anchors.topMargin: 12
            spacing: 12
            Label {
                text: "One quick review"
                font.pixelSize: 22
                font.bold: true
                color: Theme.ink
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            GlyphButton {
                glyph: "close"
                hint: "Dismiss startup review"
                enabled: !app.busy
                onClicked: dialog.requestClose()
            }
        }
    }
    function requestClose() {
        if (app.busy)
            return
        finishing = true
        app.dismissStartupReview()
        if (!app.startupReviewActive)
            close()
    }
    function finishIfReady() {
        if (!visible || app.busy)
            return
        if ((inReview || unavailable) && app.reviewedCount >= 1 && !completionHandled) {
            completionHandled = true
            requestClose()
        } else if (finishing && !app.startupReviewActive)
            close()
    }
    function syncWindowActivity() {
        if (!visible || !app.startupReviewActive || !app.reviewing || app.busy
            || app.answerRevealed || finishing)
            return
        if (!ui.active && !app.paused)
            app.pauseReview()
        else if (ui.active && app.paused)
            app.resumeReview()
    }
    onOpened: {
        startRequested = false
        finishing = false
        completionHandled = false
    }
    onClosed: {
        if (!app.busy && (app.startupReviewActive || app.startupReviewCard.variantId))
            app.dismissStartupReview()
    }
    Connections {
        target: app
        function onBusyChanged() {
            if (!app.busy && !app.startupReviewActive)
                dialog.startRequested = false
            Qt.callLater(dialog.syncWindowActivity)
            Qt.callLater(dialog.finishIfReady)
        }
        function onReviewingChanged() { Qt.callLater(dialog.syncWindowActivity) }
        function onReviewStateChanged() { Qt.callLater(dialog.finishIfReady) }
        function onAnswerRevealedChanged() {
            if (dialog.visible && dialog.inReview)
                contents.contentItem.contentY = 0
        }
        function onStartupReviewActiveChanged() {
            if (!app.startupReviewActive && dialog.finishing)
                dialog.close()
        }
        function onStartupReviewCardChanged() {
            if (dialog.visible && !app.startupReviewActive && !app.startupReviewCard.variantId)
                dialog.close()
        }
        function onLastErrorChanged() {
            if (dialog.visible && app.lastError.message) {
                dialog.finishing = false
                if (!app.startupReviewActive)
                    dialog.startRequested = false
            }
        }
    }
    Connections {
        target: dialog.ui
        function onActiveChanged() { dialog.syncWindowActivity() }
    }
    contentItem: ColumnLayout {
        spacing: 12
        Shortcut {
            sequence: "Escape"
            enabled: dialog.visible && dialog.activeFocus && !app.busy
            autoRepeat: false
            onActivated: dialog.requestClose()
        }
        Label {
            Layout.fillWidth: true
            text: dialog.card.deckName || ""
            textFormat: Text.PlainText
            color: Theme.inkMuted
            elide: Text.ElideRight
            visible: text.length > 0
        }
        Label {
            visible: !dialog.inReview && !dialog.unavailable
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.ink
            text: dialog.startRequested ? "Opening the card..."
                : dialog.card.averageResponseSeconds > 0
                    ? "Review one due card. Its past reviews averaged about "
                        + Math.max(1, Math.round(dialog.card.averageResponseSeconds)) + " seconds."
                    : "Review one short card from your queue, or leave it for later."
        }
        ErrorBlock {
            visible: dialog.unavailable
            Layout.fillWidth: true
            code: "STARTUP_REVIEW_STALE"
            message: "The selected card is no longer available in this review queue. Close this prompt and start a regular review."
            details: "Review variant: " + (app.startupReviewCard.variantId || "unavailable")
        }
        ScrollView {
            id: contents
            visible: dialog.inReview
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            contentWidth: availableWidth
            clip: true
            MarkdownPane {
                width: contents.availableWidth
                markdown: app.answerRevealed ? dialog.card.answer || "" : dialog.card.question || ""
                selectable: true
                mediaRoot: media.rootPath
                baseFontSize: 20
            }
        }
        Label {
            visible: dialog.finishing || (dialog.inReview && app.busy)
            text: dialog.finishing ? "Finishing review..." : "Saving review..."
            color: Theme.inkMuted
            Layout.fillWidth: true
        }
        Item { visible: !dialog.inReview; Layout.fillHeight: true }
    }
    footer: Flow {
        spacing: 10
        AppButton {
            text: app.startupReviewActive ? "Leave for later" : "Not now"
            enabled: !app.busy
            onClicked: dialog.requestClose()
        }
        AppButton {
            text: "Review card"
            primary: true
            visible: !dialog.inReview && !dialog.unavailable && !dialog.startRequested
            enabled: !app.busy && !dialog.startRequested && !!app.startupReviewCard.variantId
            onClicked: {
                dialog.startRequested = true
                app.startStartupReview()
            }
        }
        AppButton {
            text: "Show answer"
            primary: true
            visible: dialog.inReview && !app.answerRevealed
            enabled: !app.busy
            onClicked: app.revealAnswer()
        }
        Repeater {
            model: ["Missed", "Partial", "Hard", "Good", "Easy"]
            delegate: AppButton {
                required property string modelData
                required property int index
                text: modelData
                visible: dialog.inReview && app.answerRevealed && !dialog.finishing
                enabled: !app.busy && !app.reviewingCompletedCard
                onClicked: {
                    if (index === 1)
                        dialog.ui.openPartial()
                    else
                        app.grade(index)
                }
            }
        }
    }
}
