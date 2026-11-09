import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "generateCardDialog"
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
    title: "Generate similar card"
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
        generationInstructions.text = "";
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
        target: cardGenerator
        function onGenerated() {
            if (!dialog.visible)
                return;
            dialog.ui.editGeneratedCard(cardGenerator.draft);
            dialog.close();
        }
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
        Label {
            text: "Additional instructions (optional)"
            color: Theme.ink
            font.bold: true
        }
        ScrollView {
            id: instructionsScroll
            Layout.fillWidth: true
            Layout.preferredHeight: 100
            contentWidth: availableWidth
            clip: true
            TextArea {
                id: generationInstructions
                objectName: "generationInstructions"
                width: instructionsScroll.availableWidth
                height: Math.max(instructionsScroll.availableHeight, implicitHeight)
                enabled: !cardGenerator.busy
                wrapMode: TextEdit.Wrap
                textFormat: TextEdit.PlainText
                selectByMouse: true
                padding: 12
                color: Theme.ink
                placeholderText: "For example: use a clinical example and focus on the mechanism."
                Accessible.name: "Additional generation instructions"
                background: Rectangle {
                    radius: 8
                    color: Theme.editorField
                    border.color: generationInstructions.activeFocus ? Theme.accent : Theme.rule
                    border.width: generationInstructions.activeFocus ? 2 : 1
                }
            }
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
            objectName: "generationSourcePreview"
            id: preview
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: preview.availableWidth
                spacing: 16
                readonly property var shownCard: dialog.ui.cardById(dialog.sourceId)
                Label {
                    text: "Source card"
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
            objectName: "generateSimilarSubmit"
            text: "Generate card"
            primary: true
            enabled: !cardGenerator.busy && !app.busy
            onClicked: cardGenerator.generate(dialog.sourceId, generationInstructions.text)
        }
    }
}
