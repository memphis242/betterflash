import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: guidance
    required property var ui
    property string cardId: ""
    property string variantId: ""
    readonly property var card: ui.cardById(cardId)
    readonly property var assessment: {
        if (!card || !card.id)
            return ({});
        if (variantId && card.variantWeaknesses && card.variantWeaknesses[variantId])
            return card.variantWeaknesses[variantId];
        return card.weakness || ({});
    }
    property bool asking: false
    property string errorText: ""
    property string answerMarkdown: ""
    property string answerForCardId: ""
    property string answerForVariantId: ""
    property bool stale: false
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(760, ui.width - 2 * ui.gutter)
    height: Math.min(680, ui.height - 2 * ui.gutter)
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    padding: 24
    background: Rectangle {
        radius: 16
        color: Theme.surface
        border.color: Theme.ledgerRule
    }
    Overlay.modal: Rectangle {
        color: Theme.scrim
    }

    function showCard(id, direction) {
        cardId = id || "";
        variantId = direction || "";
        asking = false;
        errorText = "";
        answerMarkdown = "";
        answerForCardId = "";
        answerForVariantId = "";
        stale = false;
        tierHelp.close();
        if (typeof studyAssistant !== "undefined" && studyAssistant && studyAssistant.clear)
            studyAssistant.clear();
        open();
    }
    function askLanguageModel() {
        if (!card || !card.id || asking)
            return;
        if (typeof studyAssistant === "undefined" || !studyAssistant || typeof studyAssistant.requestHelp !== "function") {
            errorText = "Study guidance is not configured. Choose a language model in Settings first.";
            return;
        }
        asking = true;
        errorText = "";
        studyAssistant.requestHelp(cardId, variantId || "");
    }
    Connections {
        target: typeof studyAssistant === "undefined" ? null : studyAssistant
        function onChanged() {
            if (studyAssistant.cardId !== guidance.cardId)
                return;
            if (guidance.variantId && (studyAssistant.variantId || "") !== guidance.variantId)
                return;
            if (studyAssistant.stale && !studyAssistant.busy) {
                guidance.stale = true;
                guidance.answerMarkdown = "";
                guidance.errorText = studyAssistant.error && studyAssistant.error.message ? studyAssistant.error.code + ": " + studyAssistant.error.message : "This guidance is stale because the card or its review evidence changed. Request it again.";
                guidance.asking = false;
            } else if (studyAssistant.result && !studyAssistant.busy) {
                guidance.stale = false;
                guidance.answerForCardId = studyAssistant.cardId;
                guidance.answerForVariantId = guidance.variantId || "";
                guidance.answerMarkdown = studyAssistant.result;
                guidance.asking = false;
            }
            if (studyAssistant.error && studyAssistant.error.message && !studyAssistant.busy) {
                guidance.errorText = studyAssistant.error.code + ": " + studyAssistant.error.message;
                guidance.asking = false;
            }
        }
    }
    Popup {
        id: tierHelp
        parent: guidance.contentItem
        anchors.centerIn: parent
        width: Math.min(480, parent.width - 16)
        height: Math.min(440, parent.height - 16)
        padding: 20
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            radius: 12
            color: Theme.surfaceRaised
            border.color: Theme.rule
        }
        contentItem: ColumnLayout {
            spacing: 14
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "About recall strength"
                    Layout.fillWidth: true
                    color: Theme.ink
                    font.pixelSize: 18
                    font.bold: true
                    wrapMode: Text.Wrap
                }
                GlyphButton {
                    glyph: "close"
                    hint: "Close explanation"
                    onClicked: tierHelp.close()
                }
            }
            ScrollView {
                id: helpScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: helpScroll.availableWidth
                    spacing: 14
                    Label {
                        Layout.fillWidth: true
                        text: "Higher means stronger recall, based on recent grades, remembered answers and changes in your response times."
                        color: Theme.ink
                        font.pixelSize: 14
                        wrapMode: Text.Wrap
                    }
                    Label {
                        Layout.fillWidth: true
                        text: "Strong: above 75\nModerate: above 50 to 75\nLow: above 25 to 50\nVery low: 0 to 25"
                        color: Theme.ink
                        font.pixelSize: 14
                        lineHeight: 1.5
                    }
                    Label {
                        Layout.fillWidth: true
                        text: !guidance.assessment.assessed ? "No reviews yet; there is no rating to interpret." : guidance.assessment.provisional ? "A few more reviews will make this estimate more reliable." : "This estimates recall, not your understanding of the topic."
                        color: Theme.inkMuted
                        font.pixelSize: 14
                        wrapMode: Text.Wrap
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: !!guidance.assessment.assessed
                        text: guidance.variantId ? "This rating is for the side you are reviewing." : "This rating reflects the least-recalled side of this card."
                        color: Theme.inkMuted
                        font.pixelSize: 14
                        wrapMode: Text.Wrap
                    }
                    Repeater {
                        model: guidance.assessment.reasons || []
                        delegate: Label {
                            required property string modelData
                            Layout.fillWidth: true
                            text: modelData
                            color: Theme.ink
                            font.pixelSize: 14
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }
        }
    }
    contentItem: ColumnLayout {
        spacing: 16
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "Recall & study help"
                    color: Theme.ink
                    font.pixelSize: 21
                    font.bold: true
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    Layout.minimumWidth: 0
                }
                GlyphButton {
                    glyph: "close"
                    hint: "Close recall and study help"
                    onClicked: guidance.close()
                }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: 8
            Layout.bottomMargin: 8
            spacing: 12
            Label {
                Layout.alignment: Qt.AlignHCenter
                text: "Recall strength"
                color: Theme.inkMuted
                font.pixelSize: 16
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 12
                Item {
                    Layout.preferredWidth: 38
                    Layout.preferredHeight: 38
                }
                WeaknessBadge {
                    assessment: guidance.assessment
                    prominent: true
                    visible: true
                }
                GlyphButton {
                    glyph: "help"
                    hint: "How recall strength is calculated"
                    onClicked: tierHelp.visible ? tierHelp.close() : tierHelp.open()
                }
            }
            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: {
                    const count = Number(guidance.assessment.sampleCount || 0);
                    if (!guidance.assessment.assessed)
                        return "Review this card to see how well you recall it.";
                    if (guidance.assessment.provisional)
                        return "Early estimate based on " + count + (count === 1 ? " review." : " reviews.");
                    return "Based on your last " + count + " reviews.";
                }
                color: Theme.inkMuted
                wrapMode: Text.Wrap
                font.pixelSize: 14
            }
        }
        ScrollView {
            id: guidanceScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                width: guidanceScroll.availableWidth
                spacing: 14
                Label {
                    Layout.fillWidth: true
                    text: guidance.ui.compactText(guidance.card.front || "")
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                    color: Theme.ink
                    font.pixelSize: 16
                }
                Label {
                    Layout.fillWidth: true
                    visible: !!guidance.assessment.assessed
                    text: guidance.assessment.recommendation || ""
                    color: Theme.inkMuted
                    wrapMode: Text.Wrap
                    font.pixelSize: 14
                }
                RowLayout {
                    Layout.fillWidth: true
                    AppButton {
                        text: guidance.asking ? "Working..." : (guidance.answerMarkdown ? "Regenerate guidance" : "Ask language model")
                        primary: true
                        hint: ai.provider === "local" ? "Ask " + ai.localModelLabel + " on this device for an explanation and study ideas using this card and its review results." : "Send this card and its review results to " + ai.endpoint + " using " + ai.model + " for an explanation and study ideas."
                        enabled: !guidance.asking
                        onClicked: guidance.askLanguageModel()
                    }
                    AppButton {
                        text: "Cancel"
                        visible: guidance.asking
                        onClicked: {
                            if (typeof studyAssistant !== "undefined" && studyAssistant.cancel)
                                studyAssistant.cancel();
                            guidance.asking = false;
                        }
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                }
                TextArea {
                    visible: guidance.errorText.length > 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(58, implicitHeight)
                    readOnly: true
                    text: guidance.errorText
                    color: Theme.error
                    wrapMode: Text.Wrap
                    selectByMouse: true
                    font.family: Theme.monoFont
                    font.pixelSize: 13
                    padding: 12
                    background: Rectangle {
                        color: Theme.ledgerSurface
                        border.color: Theme.error
                        radius: 6
                    }
                }
                Label {
                    visible: guidance.stale
                    Layout.fillWidth: true
                    text: "The displayed guidance was invalidated by a card or evidence change. Request it again to refresh it."
                    color: Theme.weaknessWatch
                    wrapMode: Text.Wrap
                    font.pixelSize: 13
                }
                Rectangle {
                    visible: guidance.answerMarkdown.length > 0 && guidance.answerForCardId === guidance.cardId && guidance.answerForVariantId === (guidance.variantId || "")
                    Layout.fillWidth: true
                    implicitHeight: answerPane.implicitHeight + 28
                    radius: 8
                    color: Theme.ledgerSurface
                    border.color: Theme.rule
                    MarkdownPane {
                        id: answerPane
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 14
                        markdown: guidance.answerMarkdown
                        baseFontSize: 15
                        mediaRoot: media.rootPath
                    }
                }
            }
        }
    }
}
