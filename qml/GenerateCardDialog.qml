import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "generateCardDialog"
    required property var ui
    property string sourceId: ""
    property string deckId: ""
    readonly property bool deckGeneration: deckId.length > 0
    property bool requestStarted: false
    property bool resumePending: false
    property string pausedVariantId: ""
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(900, ui.width - 2 * ui.gutter)
    height: Math.min(deckGeneration ? 620 : 740, ui.height - 2 * ui.gutter)
    padding: 24
    modal: true
    focus: true
    title: deckGeneration ? "Generate New Card" : "Generate similar card"
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
        deckId = "";
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
    function openForDeck(id) {
        openFor("");
        deckId = id;
        generationInstructions.forceActiveFocus();
    }
    function submitGeneration() {
        if (cardGenerator.busy || app.busy)
            return;
        ui.generationDifficulty = generationDifficulty.currentValue || "similar";
        requestStarted = true;
        if (deckGeneration)
            cardGenerator.generateForDeck(deckId, generationInstructions.text);
        else
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
    contentItem: ColumnLayout {
        spacing: 10
        GenerationErrorNotice {
            objectName: "cardGenerationErrorNotice"
            message: cardGenerator.error
            operation: "generation"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(implicitHeight, dialog.availableHeight * 0.35)
            Layout.maximumHeight: dialog.availableHeight * 0.35
        }
        ScrollView {
            id: generationBody
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: generationBody.availableWidth
                spacing: 16
                Label {
                    Layout.fillWidth: true
                    text: dialog.deckGeneration
                        ? "Describe the card you want to create for this deck. The result opens in the card editor for review before saving. Models can be wrong; check the generated answer."
                        : "Create one new card that tests the same concept with a different example or question. Check the generated answer before saving."
                    wrapMode: Text.Wrap
                    color: Theme.ink
                    font.pixelSize: 16
                }
                Label {
                    Layout.fillWidth: true
                    textFormat: Text.PlainText
                    text: ai.provider === "local" ? "Uses " + ai.localModelLabel + " on this device." : (dialog.deckGeneration ? "Sends your instructions and the deck name to " : "Sends this card and its deck context to ") + ai.endpoint + " using " + ai.model + "."
                    wrapMode: Text.Wrap
                    color: Theme.inkMuted
                }
                Label {
                    visible: dialog.deckGeneration
                    Layout.fillWidth: true
                    text: "Deck: " + (dialog.ui.deckById(dialog.deckId).name || "Unavailable")
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    color: Theme.ink
                    font.bold: true
                }
                RowLayout {
                    visible: !dialog.deckGeneration
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
                Label { text: dialog.deckGeneration ? "What would you like to learn?" : "Generation instructions (optional)"; color: Theme.ink; font.bold: true }
                ScrollView {
                    id: instructionsViewport
                    Layout.fillWidth: true
                    Layout.preferredHeight: dialog.deckGeneration ? 160 : 64
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
                        placeholderText: dialog.deckGeneration
                            ? "For example: A beginner card explaining transaction durability, with a short SQL example. Include any desired question, scope, or difficulty (up to 2000 characters)."
                            : "Choose a topic, example, or constraint (up to 2000 characters)."
                        Accessible.name: "Generation instructions"
                        background: Rectangle {
                            radius: 8
                            color: Theme.editorField
                            border.color: generationInstructions.activeFocus ? Theme.accent : Theme.rule
                            border.width: generationInstructions.activeFocus ? 2 : 1
                        }
                    }
                }
                Label {
                    visible: generationInstructions.text.length > 1800
                    text: generationInstructions.text.length + " / 2000 characters" + (generationInstructions.text.length > 2000 ? ". Shorten the instructions to generate a card." : "")
                    color: generationInstructions.text.length > 2000 ? Theme.error : Theme.inkMuted
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                }
                Label {
                    visible: cardGenerator.busy
                    text: dialog.deckGeneration ? "Generating your card..." : "Generating a variation..."
                    color: Theme.ink
                    font.pixelSize: 18
                    font.bold: true
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                }
                ProgressBar {
                    visible: cardGenerator.busy
                    indeterminate: true
                    Layout.fillWidth: true
                }
                ScrollView {
                    visible: !dialog.deckGeneration
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
                && (dialog.deckGeneration
                    ? !!dialog.ui.deckById(dialog.deckId).id && generationInstructions.text.trim().length > 0
                    : !!dialog.ui.cardById(dialog.sourceId).id)
                && generationInstructions.text.length <= 2000
            onClicked: dialog.submitGeneration()
        }
    }
}
