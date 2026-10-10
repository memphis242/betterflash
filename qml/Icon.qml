import QtQuick

Canvas {
    id: icon
    property string kind: "more"
    property color stroke: Theme.ink
    property real lineWidth: 1.5
    implicitWidth: 18
    implicitHeight: 18
    onKindChanged: requestPaint()
    onStrokeChanged: requestPaint()
    onLineWidthChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onPaint: {
        const c = getContext("2d")
        c.reset()
        c.scale(width / 18, height / 18)
        c.strokeStyle = stroke
        c.lineWidth = lineWidth
        c.lineCap = "round"
        c.lineJoin = "round"
        function roundedRect(x, y, w, h, r) {
            c.moveTo(x + r, y)
            c.lineTo(x + w - r, y)
            c.quadraticCurveTo(x + w, y, x + w, y + r)
            c.lineTo(x + w, y + h - r)
            c.quadraticCurveTo(x + w, y + h, x + w - r, y + h)
            c.lineTo(x + r, y + h)
            c.quadraticCurveTo(x, y + h, x, y + h - r)
            c.lineTo(x, y + r)
            c.quadraticCurveTo(x, y, x + r, y)
            c.closePath()
        }
        switch (kind) {
        case "review-flip":
            roundedRect(3, 6, 12, 10, 1.5)
            c.moveTo(6, 10)
            c.lineTo(12, 10)
            c.moveTo(6, 13)
            c.lineTo(10, 13)
            c.moveTo(3, 4)
            c.bezierCurveTo(5, 0.5, 11, 0.5, 14, 4)
            c.moveTo(14, 1)
            c.lineTo(14, 4)
            c.lineTo(11, 4)
            break
        case "review-stack":
            roundedRect(5, 5, 11, 11, 1.5)
            c.moveTo(12, 2)
            c.lineTo(3, 2)
            c.quadraticCurveTo(2, 2, 2, 3)
            c.lineTo(2, 12)
            c.moveTo(8, 10)
            c.lineTo(10, 12)
            c.lineTo(13, 8)
            break
        case "review-ledger":
            roundedRect(3, 2, 12, 14, 1.5)
            c.moveTo(6, 5)
            c.lineTo(12, 5)
            c.moveTo(6, 8)
            c.lineTo(10, 8)
            c.moveTo(6, 12)
            c.lineTo(8, 14)
            c.lineTo(12, 10)
            break
        case "bookshelf":
            c.moveTo(1, 2)
            c.lineTo(1, 16)
            c.lineTo(17, 16)
            c.moveTo(2.8, 4)
            c.lineTo(5.3, 3.3)
            c.lineTo(8.2, 13.8)
            c.lineTo(5.7, 14.5)
            c.closePath()
            c.moveTo(6.8, 3.3)
            c.lineTo(9.3, 2.6)
            c.lineTo(12.3, 13.7)
            c.lineTo(9.8, 14.4)
            c.closePath()
            c.rect(14, 5, 3, 9.5)
            break
        case "gear-toothed":
            c.lineJoin = "miter"
            for (let i = 0; i < 8; ++i) {
                for (const point of [[-22.5, 5.7], [-12, 5.7], [-12, 7.7], [12, 7.7], [12, 5.7], [22.5, 5.7]]) {
                    const a = (i * 45 + point[0]) * Math.PI / 180
                    const x = 9 + point[1] * Math.cos(a)
                    const y = 9 + point[1] * Math.sin(a)
                    if (i === 0 && point[0] === -22.5) c.moveTo(x, y)
                    else c.lineTo(x, y)
                }
            }
            c.closePath()
            c.moveTo(11.5, 9)
            c.arc(9, 9, 2.5, 0, Math.PI * 2)
            break
        case "menu-bars":
            for (const y of [4, 9, 14]) {
                c.moveTo(3, y)
                c.lineTo(15, y)
            }
            break
        case "dock-left":
            c.moveTo(3, 3)
            c.lineTo(3, 15)
            c.moveTo(10, 5)
            c.lineTo(6, 9)
            c.lineTo(10, 13)
            c.moveTo(6, 9)
            c.lineTo(16, 9)
            break
        case "chevron-left-bold":
        case "chevron-right-bold": {
            c.lineWidth = 2.5
            const left = kind === "chevron-left-bold"
            c.moveTo(left ? 11 : 7, 4)
            c.lineTo(left ? 6 : 12, 9)
            c.lineTo(left ? 11 : 7, 14)
            break
        }
        case "panel-open":
        case "panel-close": {
            roundedRect(1.5, 2.5, 15, 13, 1.2)
            c.moveTo(6, 3)
            c.lineTo(6, 15)
            const opening = kind === "panel-open"
            c.moveTo(opening ? 9 : 13, 6)
            c.lineTo(opening ? 12 : 10, 9)
            c.lineTo(opening ? 9 : 13, 12)
            break
        }
        case "double-right":
        case "double-left": {
            const left = kind === "double-left"
            for (const x of [3, 9]) {
                c.moveTo(left ? x + 4 : x, 4)
                c.lineTo(left ? x : x + 4, 9)
                c.lineTo(left ? x + 4 : x, 14)
            }
            break
        }
        case "rail-unfold":
        case "rail-fold": {
            const opening = kind === "rail-unfold"
            c.moveTo(3, 3)
            c.lineTo(3, 15)
            c.moveTo(6, 3)
            c.lineTo(6, 15)
            c.moveTo(opening ? 10 : 15, 5)
            c.lineTo(opening ? 14 : 11, 9)
            c.lineTo(opening ? 10 : 15, 13)
            break
        }
        case "moon":
            c.moveTo(11, 2)
            c.bezierCurveTo(2, 1, 0, 13, 8, 16)
            c.bezierCurveTo(12, 17, 15, 14, 16, 11)
            c.bezierCurveTo(8, 14, 5, 5, 11, 2)
            break
        case "sun":
            c.arc(9, 9, 3, 0, Math.PI * 2)
            for (let i = 0; i < 8; i++) {
                const a = i * Math.PI / 4
                c.moveTo(9 + 6 * Math.cos(a), 9 + 6 * Math.sin(a))
                c.lineTo(9 + 8 * Math.cos(a), 9 + 8 * Math.sin(a))
            }
            break
        case "plus":
            c.moveTo(9, 3)
            c.lineTo(9, 15)
            c.moveTo(3, 9)
            c.lineTo(15, 9)
            break
        case "check":
            c.moveTo(3, 9)
            c.lineTo(7, 13)
            c.lineTo(15, 5)
            break
        case "arrow-left":
            c.moveTo(8, 4)
            c.lineTo(3, 9)
            c.lineTo(8, 14)
            c.moveTo(3, 9)
            c.lineTo(15, 9)
            break
        case "arrow-right":
            c.moveTo(10, 4)
            c.lineTo(15, 9)
            c.lineTo(10, 14)
            c.moveTo(3, 9)
            c.lineTo(15, 9)
            break
        case "up":
            c.moveTo(4, 11)
            c.lineTo(9, 6)
            c.lineTo(14, 11)
            break
        case "down":
        case "chevron":
            c.moveTo(4, 7)
            c.lineTo(9, 12)
            c.lineTo(14, 7)
            break
        case "book":
            c.moveTo(9, 4)
            c.bezierCurveTo(6, 2, 3, 3, 2, 3)
            c.lineTo(2, 15)
            c.bezierCurveTo(5, 14, 7, 14, 9, 16)
            c.bezierCurveTo(11, 14, 13, 14, 16, 15)
            c.lineTo(16, 3)
            c.bezierCurveTo(13, 2, 11, 3, 9, 4)
            c.lineTo(9, 16)
            break
        case "cards":
            c.rect(5, 2, 10, 13)
            c.moveTo(3, 5)
            c.lineTo(3, 16)
            c.lineTo(13, 16)
            break
        case "queue-tail":
            c.rect(2, 2, 6, 9)
            c.moveTo(11, 3)
            c.lineTo(15, 3)
            c.moveTo(11, 6)
            c.lineTo(15, 6)
            c.moveTo(3, 14)
            c.lineTo(14, 14)
            c.moveTo(11, 11)
            c.lineTo(14, 14)
            c.lineTo(11, 17)
            c.moveTo(16, 11)
            c.lineTo(16, 17)
            break
        case "queue-lines-tail":
            c.moveTo(2, 3)
            c.lineTo(10, 3)
            c.moveTo(2, 7)
            c.lineTo(10, 7)
            c.moveTo(2, 11)
            c.lineTo(7, 11)
            c.moveTo(2, 15)
            c.lineTo(14, 15)
            c.moveTo(13, 3)
            c.lineTo(14, 3)
            c.lineTo(14, 12)
            c.moveTo(11, 9)
            c.lineTo(14, 12)
            c.lineTo(17, 9)
            break
        case "queue-stack-tail":
            c.rect(2, 2, 7, 8)
            c.moveTo(11, 2)
            c.lineTo(11, 10)
            c.moveTo(14, 2)
            c.lineTo(14, 10)
            c.moveTo(3, 14)
            c.lineTo(15, 14)
            c.moveTo(12, 11)
            c.lineTo(15, 14)
            c.lineTo(12, 17)
            break
        case "calendar-day":
            c.rect(2, 4, 14, 12)
            c.moveTo(5, 2)
            c.lineTo(5, 6)
            c.moveTo(13, 2)
            c.lineTo(13, 6)
            c.moveTo(2, 8)
            c.lineTo(16, 8)
            c.moveTo(6, 11)
            c.lineTo(12, 11)
            c.moveTo(6, 14)
            c.lineTo(9, 14)
            break
        case "calendar-forward":
            c.moveTo(16, 9)
            c.lineTo(16, 4)
            c.lineTo(2, 4)
            c.lineTo(2, 16)
            c.lineTo(8, 16)
            c.moveTo(5, 2)
            c.lineTo(5, 6)
            c.moveTo(12, 2)
            c.lineTo(12, 6)
            c.moveTo(2, 8)
            c.lineTo(16, 8)
            c.moveTo(9, 13)
            c.lineTo(16, 13)
            c.moveTo(13, 10)
            c.lineTo(16, 13)
            c.lineTo(13, 16)
            break
        case "calendar-page":
            c.moveTo(2, 4)
            c.lineTo(16, 4)
            c.lineTo(16, 12)
            c.lineTo(12, 16)
            c.lineTo(2, 16)
            c.closePath()
            c.moveTo(5, 2)
            c.lineTo(5, 6)
            c.moveTo(13, 2)
            c.lineTo(13, 6)
            c.moveTo(2, 8)
            c.lineTo(16, 8)
            c.moveTo(16, 12)
            c.lineTo(12, 12)
            c.lineTo(12, 16)
            c.moveTo(6, 11)
            c.lineTo(8, 11)
            c.moveTo(6, 14)
            c.lineTo(8, 14)
            break
        case "return-card":
            c.rect(8, 2, 7, 12)
            c.moveTo(11, 5)
            c.lineTo(13, 5)
            c.moveTo(11, 16)
            c.lineTo(2, 16)
            c.lineTo(2, 8)
            c.lineTo(7, 8)
            c.moveTo(4, 5)
            c.lineTo(7, 8)
            c.lineTo(4, 11)
            break
        case "return-card-arrow":
            c.rect(8, 2, 8, 13)
            c.moveTo(11, 5)
            c.lineTo(13, 5)
            c.moveTo(2, 10)
            c.lineTo(7, 10)
            c.moveTo(4, 7)
            c.lineTo(7, 10)
            c.lineTo(4, 13)
            break
        case "return-bookmark":
            c.moveTo(9, 2)
            c.lineTo(15, 2)
            c.lineTo(15, 15)
            c.lineTo(12, 12)
            c.lineTo(9, 15)
            c.closePath()
            c.moveTo(7, 15)
            c.lineTo(2, 15)
            c.lineTo(2, 7)
            c.lineTo(7, 7)
            c.moveTo(4, 4)
            c.lineTo(7, 7)
            c.lineTo(4, 10)
            break
        case "clock":
            c.arc(9, 9, 7, 0, Math.PI * 2)
            c.moveTo(9, 5)
            c.lineTo(9, 9)
            c.lineTo(12, 11)
            break
        case "gear":
            c.moveTo(6, 2)
            c.lineTo(12, 2)
            c.lineTo(16, 6)
            c.lineTo(16, 12)
            c.lineTo(12, 16)
            c.lineTo(6, 16)
            c.lineTo(2, 12)
            c.lineTo(2, 6)
            c.closePath()
            c.moveTo(9, 6)
            c.arc(9, 9, 3, -Math.PI / 2, Math.PI * 1.5)
            break
        case "pause":
            c.moveTo(6, 4)
            c.lineTo(6, 14)
            c.moveTo(12, 4)
            c.lineTo(12, 14)
            break
        case "play":
            c.moveTo(6, 4)
            c.lineTo(14, 9)
            c.lineTo(6, 14)
            c.closePath()
            break
        case "mic":
            c.moveTo(6, 8)
            c.lineTo(6, 5)
            c.arc(9, 5, 3, Math.PI, 0)
            c.lineTo(12, 8)
            c.arc(9, 8, 3, 0, Math.PI)
            c.moveTo(3, 8)
            c.arc(9, 8, 6, Math.PI, 0, true)
            c.moveTo(9, 14)
            c.lineTo(9, 16)
            c.moveTo(6, 16)
            c.lineTo(12, 16)
            break
        case "search":
            c.arc(7, 7, 4.5, 0, Math.PI * 2)
            c.moveTo(10.5, 10.5)
            c.lineTo(16, 16)
            break
        case "edit":
            c.moveTo(3, 12)
            c.lineTo(12, 3)
            c.lineTo(15, 6)
            c.lineTo(6, 15)
            c.lineTo(2, 16)
            c.closePath()
            c.moveTo(10, 5)
            c.lineTo(13, 8)
            break
        case "image":
            c.rect(2, 3, 14, 12)
            c.moveTo(3, 13)
            c.lineTo(7, 8)
            c.lineTo(10, 11)
            c.lineTo(13, 7)
            c.lineTo(16, 11)
            c.moveTo(5, 6)
            c.lineTo(6, 6)
            break
        case "pin":
            c.moveTo(6, 2)
            c.lineTo(12, 2)
            c.moveTo(7, 2)
            c.lineTo(7, 7)
            c.lineTo(4, 10)
            c.lineTo(14, 10)
            c.lineTo(11, 7)
            c.lineTo(11, 2)
            c.moveTo(9, 10)
            c.lineTo(9, 17)
            break
        case "close":
            c.moveTo(4, 4)
            c.lineTo(14, 14)
            c.moveTo(14, 4)
            c.lineTo(4, 14)
            break
        case "keyboard":
            c.rect(1, 4, 16, 10)
            for (let y = 7; y <= 10; y += 3)
                for (let x = 4; x <= 14; x += 3) {
                    c.moveTo(x, y)
                    c.lineTo(x + 0.5, y)
                }
            c.moveTo(6, 12)
            c.lineTo(12, 12)
            break
        default:
            for (const x of [3, 9, 15]) {
                c.moveTo(x + 1, 9)
                c.arc(x, 9, 1, 0, Math.PI * 2)
            }
        }
        c.stroke()
    }
}
