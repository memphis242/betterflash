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
    function directionName(key) {
        switch (String(key || "")) {
        case "forward":
            return "Forward";
        case "reverse":
            return "Reverse";
        default:
            return /^c\d+$/.test(String(key)) ? "Cloze " + String(key).slice(1) : String(key || "");
        }
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
    contentItem: ColumnLayout {
        spacing: 16
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "Study guidance"
                    color: Theme.ink
                    font.pixelSize: 21
                    font.bold: true
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    Layout.minimumWidth: 0
                }
                GlyphButton {
                    glyph: "close"
                    hint: "Close study guidance"
                    onClicked: guidance.close()
                }
            }
            WeaknessBadge {
                assessment: guidance.assessment
                visible: !!guidance.assessment.tier
            }
        }
        Label {
            Layout.fillWidth: true
            text: "A bounded, deterministic estimate based on this card's review history. It helps choose what to study next; it does not measure understanding directly."
            color: Theme.inkMuted
            wrapMode: Text.Wrap
            font.pixelSize: 13
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
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: reasonContent.implicitHeight + 32
                    radius: 8
                    color: Theme.ledgerSurface
                    border.color: Theme.rule
                    ColumnLayout {
                        id: reasonContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 16
                        spacing: 9
                        Label {
                            text: "Why this card has this tier"
                            color: Theme.ink
                            font.pixelSize: 15
                            font.bold: true
                        }
                        Label {
                            Layout.fillWidth: true
                            text: guidance.assessment.recommendation || "There is not enough review history to assess this card yet."
                            color: Theme.inkMuted
                            wrapMode: Text.Wrap
                            font.pixelSize: 13
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: !!guidance.assessment.variantKey || !!guidance.assessment.variantId
                            text: (guidance.variantId ? "Review direction: " : "Weakest reviewed direction: ") + guidance.directionName(guidance.assessment.variantKey)
                            color: Theme.ink
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }
                        Label {
                            Layout.fillWidth: true
                            text: {
                                const reviewCount = Number(guidance.assessment.reviewCount || 0);
                                const sampleCount = Number(guidance.assessment.sampleCount || 0);
                                const score = Number(guidance.assessment.score);
                                const parts = [];
                                if (guidance.assessment.assessed && Number.isFinite(score))
                                    parts.push("Priority score: " + Math.round(score) + "/100");
                                if (reviewCount > 0)
                                    parts.push("Reviews: " + reviewCount);
                                if (sampleCount > 0)
                                    parts.push("Evidence samples: " + sampleCount);
                                return parts.join("  |  ");
                            }
                            visible: text.length > 0
                            color: Theme.inkMuted
                            font.family: Theme.monoFont
                            font.pixelSize: 12
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: !!guidance.assessment.provisional
                            text: "Provisional: this estimate has fewer than three review observations."
                            color: Theme.weaknessWatch
                            wrapMode: Text.Wrap
                            font.pixelSize: 13
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Repeater {
                                model: guidance.assessment.reasons || []
                                delegate: Label {
                                    required property string modelData
                                    Layout.fillWidth: true
                                    text: modelData
                                    color: Theme.ink
                                    wrapMode: Text.Wrap
                                    font.family: Theme.monoFont
                                    font.pixelSize: 13
                                    padding: 7
                                    background: Rectangle {
                                        radius: 4
                                        color: Theme.surfaceRaised
                                        border.color: Theme.rule
                                    }
                                }
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            visible: (guidance.assessment.factors || []).length > 0
                            Label {
                                text: "Factors"
                                color: Theme.ink
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                            }
                            Repeater {
                                model: guidance.assessment.factors || []
                                delegate: Label {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    text: modelData.label + ": " + Math.round(Number(modelData.value || 0)) + "/100  -  " + (modelData.detail || "")
                                    color: Theme.inkMuted
                                    wrapMode: Text.Wrap
                                    font.family: Theme.monoFont
                                    font.pixelSize: 12
                                }
                            }
                        }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: "Tiers: Steady below 25, Watch from 25, Fragile from 50, Needs study from 75. The score combines recent recall, difficult ratings, deterioration and a small personal-speed signal."
                    color: Theme.inkMuted
                    wrapMode: Text.Wrap
                    font.pixelSize: 12
                }
                Label {
                    text: "Language model assistance"
                    color: Theme.ink
                    font.pixelSize: 15
                    font.bold: true
                }
                Label {
                    Layout.fillWidth: true
                    text: ai.provider === "local" ? "On this device using " + ai.localModelLabel + ". The model receives this card's question, answer, deck name, tags and review signals when you ask." : "Sends this card's question, answer, deck name, tags and review signals to " + ai.endpoint + " using " + ai.model + " when you ask."
                    color: Theme.inkMuted
                    wrapMode: Text.Wrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    AppButton {
                        text: guidance.asking ? "Working..." : (guidance.answerMarkdown ? "Regenerate guidance" : "Ask language model")
                        primary: true
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
