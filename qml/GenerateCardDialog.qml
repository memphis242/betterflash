import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    required property var ui
    property string sourceId: ""
    property bool resumePending: false
    property string pausedVariantId: ""
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(900, ui.width - 2 * ui.gutter)
    height: Math.min(740, ui.height - 2 * ui.gutter)
    padding: 24
    modal: true
    focus: true
    title: "Generate like this"
    closePolicy: Popup.CloseOnEscape
    background: Rectangle {
        radius: 16
        color: Theme.surface
        border.color: Theme.ledgerRule
    }
    Overlay.modal: Rectangle {
        color: Theme.scrim
    }

    function openFor(cardId) {
        sourceId = cardId;
        cardGenerator.clear();
        resumePending = ui.page === 0 && app.reviewing && !app.paused;
        pausedVariantId = app.currentCard.variantId || "";
        if (resumePending)
            app.pauseReview();
        open();
    }
    function resumeIfReady() {
        if (!resumePending || visible || ui.generationContextOpen || app.busy)
            return;
        resumePending = false;
        if (ui.page === 0 && app.reviewing && app.paused && app.currentCard.variantId === pausedVariantId)
            app.resumeReview();
    }
    onClosed: {
        cardGenerator.cancel();
        Qt.callLater(resumeIfReady);
    }
    Connections {
        target: app
        function onBusyChanged() {
            dialog.resumeIfReady();
        }
    }
    Connections {
        target: dialog.ui
        function onGenerationContextOpenChanged() {
            Qt.callLater(dialog.resumeIfReady);
        }
    }
    contentItem: ColumnLayout {
        spacing: 16
        Label {
            Layout.fillWidth: true
            text: "Create one new card that tests the same concept with a different example or question. Check the generated answer before saving."
            wrapMode: Text.Wrap
            color: Theme.ink
            font.pixelSize: 16
        }
        Label {
            Layout.fillWidth: true
            textFormat: Text.PlainText
            text: ai.provider === "local" ? "Uses " + ai.localModelLabel + " on this device." : "Sends this card and its deck context to " + ai.endpoint + " using " + ai.model + "."
            wrapMode: Text.Wrap
            color: Theme.inkMuted
        }
        ErrorBlock {
            Layout.fillWidth: true
            message: cardGenerator.error
        }
        Label {
            visible: cardGenerator.busy
            text: "Generating a variation..."
            color: Theme.inkMuted
        }
        ScrollView {
            id: preview
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: preview.availableWidth
                spacing: 16
                readonly property var shownCard: cardGenerator.draft.front ? cardGenerator.draft : dialog.ui.cardById(dialog.sourceId)
                Label {
                    text: cardGenerator.draft.front ? "New card draft" : "Source card"
                    color: Theme.accent
                    font.pixelSize: 18
                    font.bold: true
                }
                Label {
                    text: "Question"
                    color: Theme.inkMuted
                    font.pixelSize: 16
                }
                MarkdownPane {
                    markdown: parent.shownCard.front || ""
                    mediaRoot: media.rootPath
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    baseFontSize: 19
                }
                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: Theme.rule
                }
                Label {
                    text: "Answer"
                    color: Theme.inkMuted
                    font.pixelSize: 16
                }
                MarkdownPane {
                    markdown: parent.shownCard.back || ""
                    mediaRoot: media.rootPath
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    baseFontSize: 19
                }
            }
        }
    }
    footer: RowLayout {
        spacing: 12
        AppButton {
            text: cardGenerator.busy ? "Cancel request" : "Close"
            onClicked: dialog.close()
        }
        Item {
            Layout.fillWidth: true
        }
        AppButton {
            text: cardGenerator.draft.front ? "Generate another" : "Generate card"
            enabled: !cardGenerator.busy && !app.busy
            onClicked: cardGenerator.generate(dialog.sourceId)
        }
        AppButton {
            text: "Edit and save draft"
            primary: true
            visible: !!cardGenerator.draft.front
            enabled: !cardGenerator.busy && !app.busy
            onClicked: {
                dialog.ui.editGeneratedCard(cardGenerator.draft);
                dialog.close();
            }
        }
    }
}
