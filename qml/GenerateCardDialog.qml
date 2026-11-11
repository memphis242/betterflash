import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "generateCardDialog"
    required property var ui
    property string sourceId: ""
    property bool requestStarted: false
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
    closePolicy: Popup.NoAutoClose
    header: Item {
        implicitHeight: 64
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: dialog.padding
            anchors.rightMargin: dialog.padding
            anchors.topMargin: 14
            spacing: 12
            Label {
                text: dialog.title
                color: Theme.ink
                font.pixelSize: 22
                font.bold: true
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            GlyphButton { glyph: "close"; hint: "Close card generation"; onClicked: dialog.close() }
        }
    }
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
        requestStarted = false;
        ui.generationDifficulty = "similar";
        generationDifficulty.currentIndex = 0;
        generationInstructions.text = "";
        resumePending = ui.page === 0 && app.reviewing && !app.paused;
        pausedVariantId = app.currentCard.variantId || "";
        if (resumePending)
            app.pauseReview();
        open();
    }
    function submitGeneration() {
        if (cardGenerator.busy || app.busy)
            return;
        ui.generationDifficulty = generationDifficulty.currentValue || "similar";
        requestStarted = true;
        cardGenerator.generate(sourceId, generationInstructions.text, ui.generationDifficulty);
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
    contentItem: ScrollView {
        id: generationBody
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
            width: generationBody.availableWidth
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
            RowLayout {
                Layout.fillWidth: true
                Label { text: "Difficulty"; color: Theme.ink }
                AppComboBox {
                    id: generationDifficulty
                    objectName: "generationDifficulty"
                    model: dialog.ui.generationDifficultyChoices
                    textRole: "label"
                    valueRole: "key"
                    Layout.fillWidth: true
                    enabled: !cardGenerator.busy
                    comboHint: "Requested challenge relative to the source card. Review scheduling is unchanged."
                }
            }
            Label { text: "Generation instructions (optional)"; color: Theme.ink }
            ScrollView {
                id: instructionsViewport
                Layout.fillWidth: true
                Layout.preferredHeight: 64
                contentWidth: availableWidth
                clip: true
                TextArea {
                    id: generationInstructions
                    objectName: "generationInstructions"
                    width: instructionsViewport.availableWidth
                    height: Math.max(instructionsViewport.availableHeight, implicitHeight)
                    readOnly: cardGenerator.busy
                    textFormat: TextEdit.PlainText
                    wrapMode: TextEdit.Wrap
                    selectByMouse: true
                    padding: 10
                    color: Theme.ink
                    placeholderText: "Choose a topic, example, or constraint (up to 2000 characters)."
                    Accessible.name: "Generation instructions"
                    background: Rectangle {
                        radius: 8
                        color: Theme.editorField
                        border.color: generationInstructions.activeFocus ? Theme.accent : Theme.rule
                        border.width: generationInstructions.activeFocus ? 2 : 1
                    }
                }
            }
            ScrollView {
                id: generationErrors
                visible: !!cardGenerator.error
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(80, generationError.implicitHeight)
                contentWidth: availableWidth
                clip: true
                ErrorBlock {
                    id: generationError
                    width: generationErrors.availableWidth
                    message: cardGenerator.error
                }
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
                Layout.preferredHeight: Math.max(120, Math.min(260, dialog.availableHeight * 0.4))
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
    }
    footer: RowLayout {
        spacing: 12
        AppButton {
            text: cardGenerator.busy ? "Cancel request" : "Cancel"
            onClicked: dialog.close()
        }
        Item {
            Layout.fillWidth: true
        }
        AppButton {
            objectName: "generateSimilarSubmit"
            text: dialog.requestStarted && cardGenerator.error ? "Retry generation" : "Generate card"
            primary: true
            enabled: !app.busy && !cardGenerator.busy
                && !!dialog.ui.cardById(dialog.sourceId).id
            onClicked: dialog.submitGeneration()
        }
    }
}
