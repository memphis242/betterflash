import QtQuick
import QtQuick.Controls

Item {
    id: strip
    required property var cards
    required property int reviewedCount
    implicitHeight: 10
    Accessible.role: Accessible.ProgressBar
    Accessible.name: reviewedCount + " of " + cards.length + " cards reviewed"
    readonly property var outcomeColors: [Theme.recallMissed, Theme.recallPartial, Theme.recallHard, Theme.recallGood, Theme.recallEasy]
    readonly property color ruleColor: Theme.rule
    onCardsChanged: drawing.requestPaint()
    onOutcomeColorsChanged: drawing.requestPaint()
    onRuleColorChanged: drawing.requestPaint()
    function gradeAt(index) {
        return index >= 0 && index < cards.length ? Number(cards[index].sessionGrade) : -1;
    }
    Canvas {
        id: drawing
        anchors.fill: parent
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();
            if (!strip.cards.length || width <= 0)
                return;
            const step = width / strip.cards.length;
            const gap = Math.min(3, step * 0.18);
            const segment = step - gap;
            for (let i = 0; i < strip.cards.length; ++i) {
                const grade = strip.gradeAt(i);
                const x = i * step;
                if (grade >= 0 && grade < strip.outcomeColors.length) {
                    ctx.fillStyle = strip.outcomeColors[grade];
                    ctx.fillRect(x, 1, segment, height - 2);
                } else {
                    ctx.strokeStyle = strip.ruleColor;
                    ctx.lineWidth = Math.min(1, segment / 3);
                    ctx.strokeRect(x + ctx.lineWidth / 2, 1.5, segment - ctx.lineWidth, height - 3);
                }
            }
        }
    }
    MouseArea {
        id: hover
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        hoverEnabled: true
        readonly property int cardIndex: Math.min(strip.cards.length - 1, Math.floor(mouseX / Math.max(1, width) * strip.cards.length))
        ToolTip.visible: containsMouse && strip.cards.length > 0
        ToolTip.text: "Card " + (cardIndex + 1) + " of " + strip.cards.length + ": " + (["Missed", "Partial", "Hard", "Good", "Easy"][strip.gradeAt(cardIndex)] || "Not reviewed")
    }
}
