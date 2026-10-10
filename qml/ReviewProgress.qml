import QtQuick
import QtQuick.Controls

Item {
    id: strip
    required property var cards
    required property int reviewedCount
    implicitHeight: 26
    Accessible.role: Accessible.ProgressBar
    Accessible.name: reviewedCount + " of " + cards.length + " cards reviewed"
    readonly property var outcomeColors: [Theme.recallMissed, Theme.recallPartial, Theme.recallHard, Theme.recallGood, Theme.recallEasy]
    readonly property color trackColor: Theme.surfaceRaised
    onCardsChanged: drawing.requestPaint()
    onOutcomeColorsChanged: drawing.requestPaint()
    onTrackColorChanged: drawing.requestPaint()
    function gradeAt(index) {
        return index >= 0 && index < cards.length ? Number(cards[index].sessionGrade) : -1;
    }
    function cardAtX(x) {
        const cardCount = cards.length;
        if (!cardCount || width <= 0)
            return -1;
        const arrowWidth = Math.min(width, Math.max(8, Math.min(24, width * 0.1)));
        const bodyWidth = Math.max(0, width - arrowWidth);
        if (x >= bodyWidth || bodyWidth <= 0)
            return cardCount - 1;
        return Math.max(0, Math.min(cardCount - 1, Math.floor(x / bodyWidth * cardCount)));
    }
    Canvas {
        id: drawing
        anchors.fill: parent
        antialiasing: true
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();
            if (!strip.cards.length || width <= 0 || height <= 0)
                return;
            const cardCount = strip.cards.length;
            const arrowWidth = Math.min(width, Math.max(8, Math.min(24, width * 0.1)));
            const bodyWidth = Math.max(0, width - arrowWidth);
            const segment = bodyWidth / cardCount;
            const gapWidth = Math.min(4, segment * 0.18);
            const slant = Math.min(14, segment * 0.34);
            const midpoint = height / 2;

            // The track is one continuous silhouette: square at the left and pointed at the right.
            ctx.fillStyle = strip.trackColor;
            ctx.beginPath();
            ctx.moveTo(0, 0);
            ctx.lineTo(bodyWidth, 0);
            ctx.lineTo(width, midpoint);
            ctx.lineTo(bodyWidth, height);
            ctx.lineTo(0, height);
            ctx.closePath();
            ctx.fill();

            // Keep fills and transparent cuts inside the arrow-shaped silhouette.
            ctx.save();
            ctx.beginPath();
            ctx.moveTo(0, 0);
            ctx.lineTo(bodyWidth, 0);
            ctx.lineTo(width, midpoint);
            ctx.lineTo(bodyWidth, height);
            ctx.lineTo(0, height);
            ctx.closePath();
            ctx.clip();

            for (let i = 0; i < cardCount; ++i) {
                const grade = strip.gradeAt(i);
                if (grade >= 0 && grade < strip.outcomeColors.length) {
                    const x0 = i * segment;
                    const x1 = (i + 1) * segment;
                    ctx.fillStyle = strip.outcomeColors[grade];
                    const topLeft = i === 0 ? x0 : x0 + slant / 2;
                    const bottomLeft = i === 0 ? x0 : x0 - slant / 2;
                    const topRight = i === cardCount - 1 ? x1 : x1 + slant / 2;
                    const bottomRight = i === cardCount - 1 ? x1 : x1 - slant / 2;
                    ctx.beginPath();
                    ctx.moveTo(bottomLeft, height);
                    ctx.lineTo(topLeft, 0);
                    ctx.lineTo(topRight, 0);
                    ctx.lineTo(bottomRight, height);
                    ctx.closePath();
                    ctx.fill();

                    // The final completed slot owns the arrowhead.
                    if (i === cardCount - 1) {
                        ctx.beginPath();
                        ctx.moveTo(bodyWidth, 0);
                        ctx.lineTo(width, midpoint);
                        ctx.lineTo(bodyWidth, height);
                        ctx.closePath();
                        ctx.fill();
                    }
                }
            }

            // Erase a constant-width slanted cut only where at least one side is complete.
            if (gapWidth > 0 && segment > 0) {
                ctx.globalCompositeOperation = "destination-out";
                for (let boundary = 1; boundary < cardCount; ++boundary) {
                    const leftComplete = strip.gradeAt(boundary - 1) >= 0 && strip.gradeAt(boundary - 1) < strip.outcomeColors.length;
                    const rightComplete = strip.gradeAt(boundary) >= 0 && strip.gradeAt(boundary) < strip.outcomeColors.length;
                    if (!leftComplete && !rightComplete)
                        continue;
                    const center = boundary * segment;
                    const topCenter = center + slant / 2;
                    const bottomCenter = center - slant / 2;
                    ctx.beginPath();
                    ctx.moveTo(topCenter - gapWidth / 2, 0);
                    ctx.lineTo(topCenter + gapWidth / 2, 0);
                    ctx.lineTo(bottomCenter + gapWidth / 2, height);
                    ctx.lineTo(bottomCenter - gapWidth / 2, height);
                    ctx.closePath();
                    ctx.fill();
                }
                ctx.globalCompositeOperation = "source-over";
            }
            ctx.restore();
        }
    }
    MouseArea {
        id: hover
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        hoverEnabled: true
        readonly property int cardIndex: strip.cardAtX(mouseX)
        ToolTip.visible: containsMouse && strip.cards.length > 0
        ToolTip.text: "Card " + (cardIndex + 1) + " of " + strip.cards.length + ": " + (["Missed", "Partial", "Hard", "Good", "Easy"][strip.gradeAt(cardIndex)] || "Not reviewed")
    }
}
