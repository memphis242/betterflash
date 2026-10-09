import QtQuick
import QtQuick.Controls

Item {
    id: bar

    property int variant: 0
    property int cardCount: 12
    property int reviewedCount: 7
    property var grades: [4, 3, 2, 1, 0, 3, 4]
    implicitHeight: variant === 1 ? 34 : variant === 2 ? 30 : 26
    Accessible.role: Accessible.ProgressBar
    Accessible.name: reviewedCount + " of " + cardCount + " cards reviewed"

    readonly property var outcomeColors: [Theme.recallMissed, Theme.recallPartial,
        Theme.recallHard, Theme.recallGood, Theme.recallEasy]
    readonly property color neutralColor: variant === 1 ? Theme.ledgerSurfaceRaised : Theme.surfaceRaised
    readonly property color outerRule: variant === 1 ? Theme.ledgerRule : Theme.rule

    function gradeAt(index) {
        if (index < 0 || index >= reviewedCount || index >= grades.length)
            return -1
        const grade = Number(grades[index])
        return grade >= 0 && grade < outcomeColors.length ? grade : -1
    }

    function cardAtX(x) {
        if (cardCount <= 0 || width <= 0)
            return -1
        const inset = variant === 1 ? 6 : 0
        const right = Math.max(inset, width - inset)
        const arrowWidth = Math.min(right - inset, Math.max(8, Math.min(24, width * 0.1)))
        const bodyWidth = Math.max(0, right - inset - arrowWidth)
        if (x <= inset || bodyWidth <= 0)
            return 0
        return x >= inset + bodyWidth ? cardCount - 1 : Math.max(0, Math.min(cardCount - 1,
            Math.floor((x - inset) / bodyWidth * cardCount)))
    }

    onReviewedCountChanged: drawing.requestPaint()
    onCardCountChanged: drawing.requestPaint()
    onGradesChanged: drawing.requestPaint()
    onVariantChanged: drawing.requestPaint()
    onOutcomeColorsChanged: drawing.requestPaint()
    onNeutralColorChanged: drawing.requestPaint()
    onOuterRuleChanged: drawing.requestPaint()

    Canvas {
        id: drawing
        anchors.fill: parent
        antialiasing: true
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const count = bar.cardCount
            if (count <= 0 || width <= 0 || height <= 0)
                return

            const inset = bar.variant === 1 ? 6 : 0
            const right = Math.max(inset, width - inset)
            const arrowWidth = Math.min(right - inset, Math.max(8, Math.min(24, width * 0.1)))
            const bodyWidth = Math.max(0, right - inset - arrowWidth)
            const segment = bodyWidth / count
            const middle = height / 2
            const gap = bar.variant === 2 ? Math.min(7, segment * 0.32) : Math.min(4, segment * 0.18)
            const slant = bar.variant === 2 ? Math.min(13, segment * 0.48) : Math.min(10, segment * 0.34)

            function silhouette(left, top, right, bottom) {
                const tip = right
                const shoulder = Math.max(left, right - arrowWidth)
                ctx.beginPath()
                ctx.moveTo(left, top)
                ctx.lineTo(shoulder, top)
                ctx.lineTo(tip, (top + bottom) / 2)
                ctx.lineTo(shoulder, bottom)
                ctx.lineTo(left, bottom)
                ctx.closePath()
            }

            if (bar.variant === 1) {
                // A recessed ledger strip leaves a quiet margin inside its outlined casing.
                ctx.fillStyle = bar.neutralColor
                silhouette(0, 0, width, height)
                ctx.fill()
                ctx.strokeStyle = bar.outerRule
                ctx.lineWidth = 1
                silhouette(0.5, 0.5, width - 0.5, height - 0.5)
                ctx.stroke()
                ctx.save()
                ctx.beginPath()
                ctx.rect(inset, inset, Math.max(0, right - inset), Math.max(0, height - inset * 2))
                ctx.clip()
                ctx.fillStyle = Theme.ledgerSurface
                ctx.fillRect(inset, inset, Math.max(0, right - inset), Math.max(0, height - inset * 2))
                ctx.restore()
            } else {
                ctx.fillStyle = bar.neutralColor
                silhouette(0, 0, width, height)
                ctx.fill()
            }

            ctx.save()
            silhouette(0, 0, width, height)
            ctx.clip()
            const fillTop = inset
            const fillBottom = height - inset
            const fillHeight = Math.max(0, fillBottom - fillTop)
            for (let i = 0; i < count; ++i) {
                const grade = bar.gradeAt(i)
                if (grade < 0)
                    continue
                const x0 = i * segment + inset
                const x1 = (i + 1) * segment + inset
                const isLast = i === count - 1
                ctx.fillStyle = bar.outcomeColors[grade]
                ctx.beginPath()
                if (bar.variant === 2) {
                    const leftTop = i === 0 ? x0 : x0 + slant / 2
                    const leftMiddle = i === 0 ? x0 : x0 - slant / 2
                    const rightTop = isLast ? x1 : x1 - slant / 2
                    const rightMiddle = isLast ? x1 : x1 + slant / 2
                    ctx.moveTo(leftMiddle, middle)
                    ctx.lineTo(leftTop, fillTop)
                    ctx.lineTo(rightTop, fillTop)
                    ctx.lineTo(rightMiddle, middle)
                    ctx.lineTo(isLast ? x1 : x1 - slant / 2, fillBottom)
                    ctx.lineTo(i === 0 ? x0 : x0 + slant / 2, fillBottom)
                } else {
                    const topLeft = i === 0 ? x0 : x0 + slant / 2
                    const bottomLeft = i === 0 ? x0 : x0 - slant / 2
                    const topRight = isLast ? x1 : x1 + slant / 2
                    const bottomRight = isLast ? x1 : x1 - slant / 2
                    ctx.moveTo(bottomLeft, fillBottom)
                    ctx.lineTo(topLeft, fillTop)
                    ctx.lineTo(topRight, fillTop)
                    ctx.lineTo(bottomRight, fillBottom)
                }
                ctx.closePath()
                ctx.fill()
                if (isLast) {
                    ctx.beginPath()
                    ctx.moveTo(bodyWidth + inset, fillTop)
                    ctx.lineTo(right, middle)
                    ctx.lineTo(bodyWidth + inset, fillBottom)
                    ctx.closePath()
                    ctx.fill()
                }
            }

            if (gap > 0 && segment > 0) {
                ctx.globalCompositeOperation = "destination-out"
                for (let boundary = 1; boundary < Math.min(count, reviewedCount + 1); ++boundary) {
                    const completedLeft = bar.gradeAt(boundary - 1) >= 0
                    const completedRight = bar.gradeAt(boundary) >= 0
                    if (!completedLeft && !completedRight)
                        continue
                    const center = boundary * segment + inset
                    const topCenter = center + slant / 2
                    const bottomCenter = center - slant / 2
                    ctx.beginPath()
                    if (bar.variant === 2) {
                        ctx.moveTo(center - gap / 2, fillTop)
                        ctx.lineTo(center + gap / 2, fillTop)
                        ctx.lineTo(center + gap / 2, middle)
                        ctx.lineTo(center + gap / 2, fillBottom)
                        ctx.lineTo(center - gap / 2, fillBottom)
                        ctx.lineTo(center - gap / 2, middle)
                    } else {
                        ctx.moveTo(topCenter - gap / 2, fillTop)
                        ctx.lineTo(topCenter + gap / 2, fillTop)
                        ctx.lineTo(bottomCenter + gap / 2, fillBottom)
                        ctx.lineTo(bottomCenter - gap / 2, fillBottom)
                    }
                    ctx.closePath()
                    ctx.fill()
                }
                ctx.globalCompositeOperation = "source-over"
            }
            ctx.restore()

            if (bar.variant === 1) {
                // Keep the casing visible over the recessed fill and its transparent cuts.
                ctx.strokeStyle = bar.outerRule
                ctx.lineWidth = 1
                silhouette(0.5, 0.5, width - 0.5, height - 0.5)
                ctx.stroke()
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        hoverEnabled: true
        readonly property int slot: bar.cardAtX(mouseX)
        ToolTip.visible: containsMouse && slot >= 0
        ToolTip.text: "Card " + (slot + 1) + " of " + bar.cardCount + ": "
            + (["Missed", "Partial", "Hard", "Good", "Easy"][bar.gradeAt(slot)] || "Not reviewed")
    }
}
