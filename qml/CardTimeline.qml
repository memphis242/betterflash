import QtQuick
import QtQuick.Controls

Item {
    id: timeline

    property int variant: 0
    property var events: []
    property bool hasMore: false
    property bool loading: false
    signal loadOlderRequested()

    implicitHeight: 242
    height: implicitHeight
    clip: true
    focus: true
    activeFocusOnTab: true
    Accessible.role: Accessible.ScrollBar
    Accessible.name: "Card review timeline"

    readonly property int ticketWidth: variant === 0 ? 216 : 206
    readonly property int ticketGap: 16
    readonly property int eventStep: ticketWidth + ticketGap
    readonly property int trackY: height * 0.58
    readonly property color neutralMark: Theme.inkMuted
    readonly property color spine: Theme.ledgerRule
    readonly property var outcomeColors: [Theme.recallMissed, Theme.recallPartial,
        Theme.recallHard, Theme.recallGood, Theme.recallEasy]

    function gradeColor(value) {
        const grade = Number(value)
        return grade >= 0 && grade < outcomeColors.length ? outcomeColors[grade] : Theme.inkMuted
    }

    function gradeLabel(value) {
        switch (Number(value)) {
        case 0: return "Missed"
        case 1: return "Partial"
        case 2: return "Hard"
        case 3: return "Good"
        case 4: return "Easy"
        default: return "Unreviewed"
        }
    }

    function dateTime(value) {
        const date = new Date(String(value || ""))
        if (Number.isNaN(date.getTime()))
            return String(value || "Unknown time")
        return Qt.formatDateTime(date, "yyyy-MM-dd HH:mm")
    }

    function eventCaption(event) {
        switch (String(event.kind || "")) {
        case "saved":
        case "created": return "First saved"
        case "modified": return "Edited"
        case "due": return event.label ? String(event.label) : "Next review"
        case "now": return "Now"
        default: return gradeLabel(event.grade)
        }
    }

    function eventIsReview(event) {
        return String(event.kind || "") === "review"
    }

    function eventIsFuture(event) {
        return String(event.kind || "") === "due"
    }

    function eventDetails(event) {
        const parts = [dateTime(event.at), eventCaption(event)]
        if (String(event.kind || "") === "saved" || String(event.kind || "") === "created")
            parts[1] = "First saved on this device; original creation date may be earlier"
        if (eventIsReview(event)) {
            const direction = event.variantKey ? " - " + variantLabel(event.variantKey) : " - Review variant"
            parts.push("Recall " + Number(event.recallFraction || 0).toFixed(2) + direction)
            parts.push(Number(event.responseSeconds || 0).toFixed(1) + " s")
        }
        return parts.join(" - ")
    }

    function variantLabel(value) {
        const key = String(value || "").toLowerCase()
        if (key === "forward")
            return "Forward"
        if (key === "reverse" || key === "back")
            return "Reverse"
        if (key === "cloze" || key.indexOf("cloze") === 0)
            return key === "cloze" ? "Cloze" : "Cloze " + key.substring(5)
        return "Review variant"
    }

    function recallY(event) {
        const fraction = Math.max(0, Math.min(1, Number(event.recallFraction || 0)))
        return 106 - fraction * 64
    }

    function maxScroll() {
        return Math.max(0, reviewEvents.contentWidth - reviewEvents.width)
    }

    function moveToLatest() {
        Qt.callLater(function() {
            if (events.length > 0)
                reviewEvents.positionViewAtEnd()
        })
    }

    property string previousFirstId: ""
    property string previousLastId: ""
    property string previousEventSignature: ""
    property int previousEventCount: 0
    onEventsChanged: {
        const priorContentX = reviewEvents.contentX
        const previousSignature = previousEventSignature
        const priorCount = previousEventCount
        const priorLastId = previousLastId
        const currentSignature = events.map(function(event) { return String(event.id || "") }).join("|")
        const firstNow = events.length > 0 ? String(events[0].id || "") : ""
        const lastNow = events.length > 0 ? String(events[events.length - 1].id || "") : ""
        const prepended = previousFirstId.length > 0 && firstNow !== previousFirstId && events.length > priorCount
        const cardChanged = previousSignature.length > 0 && currentSignature !== previousSignature
        previousEventSignature = currentSignature
        previousFirstId = events.length > 0 ? String(events[0].id || "") : ""
        previousLastId = lastNow
        previousEventCount = events.length
        if (prepended) {
            const added = events.length - priorCount
            Qt.callLater(function() { reviewEvents.contentX = Math.min(maxScroll(), priorContentX + added * eventStep) })
        } else if (!previousSignature || cardChanged && priorLastId !== lastNow) {
            moveToLatest()
        } else {
            Qt.callLater(function() { reviewEvents.contentX = Math.min(maxScroll(), priorContentX) })
        }
    }
    onVariantChanged: moveToLatest()

    Rectangle {
        anchors.fill: parent
        color: Theme.ledgerSurface
        border.width: 1
        border.color: Theme.ledgerRule
        radius: 12
    }

    Label {
        id: heading
        x: 18
        y: 12
        width: Math.max(0, loadEarlier.visible ? loadEarlier.x - x - 12 : timeline.width - 36)
        elide: Text.ElideRight
        text: variant === 0 ? "01  Review ledger" : variant === 1 ? "02  Recall trace" : "03  Chapter ribbon"
        color: Theme.inkMuted
        font.family: Theme.monoFont
        font.pixelSize: 12
        font.bold: true
        font.letterSpacing: 1.1
    }

    Label {
        x: heading.x
        y: heading.y + 17
        width: heading.width
        elide: Text.ElideRight
        text: "Events in order - gaps not to scale"
        color: Theme.inkMuted
        font.family: Theme.monoFont
        font.pixelSize: 11
        opacity: 1
    }

    AppButton {
        id: loadEarlier
        visible: timeline.hasMore
        x: Math.max(18, timeline.width - width - 18)
        y: 8
        text: loading ? "Loading..." : "Earlier"
        hint: loading ? "Loading earlier events" : "Load earlier events"
        enabled: !timeline.loading
        font.pixelSize: 12
        z: 4
        onClicked: timeline.loadOlderRequested()
    }

    ListView {
        id: reviewEvents
        objectName: "cardTimelineEvents"
        x: 14
        y: 52
        width: Math.max(0, parent.width - x - 14)
        height: Math.max(0, parent.height - y - 32)
        model: timeline.events
        orientation: ListView.Horizontal
        spacing: timeline.ticketGap
        clip: true
        interactive: true
        boundsBehavior: Flickable.StopAtBounds
        cacheBuffer: width * 2
        keyNavigationEnabled: false
        ScrollBar.horizontal: ScrollBar {
            id: horizontalBar
            policy: ScrollBar.AlwaysOn
            height: 8
            contentItem: Rectangle {
                implicitHeight: 6
                radius: 3
                color: horizontalBar.pressed ? Theme.accent : Theme.ledgerRule
            }
            background: Rectangle {
                implicitHeight: 6
                radius: 3
                color: Theme.surfaceRaised
            }
        }

        delegate: Item {
            id: eventItem
            required property var modelData
            required property int index
            width: timeline.ticketWidth
            height: reviewEvents.height - 12
            Accessible.role: Accessible.ListItem
            Accessible.name: timeline.eventDetails(modelData)

            Rectangle {
                visible: timeline.variant === 0
                x: 0
                y: 26
                width: parent.width
                height: 1
                color: timeline.spine
                opacity: 0.8
            }
            Rectangle {
                visible: timeline.variant === 0
                x: 0
                y: 20
                width: 9
                height: 12
                radius: 4
                color: timeline.eventIsFuture(modelData) ? Theme.transparent : timeline.gradeColor(modelData.grade)
                border.width: 2
                border.color: timeline.eventIsFuture(modelData) ? timeline.neutralMark : timeline.gradeColor(modelData.grade)
            }
            Rectangle {
                visible: timeline.variant === 0
                x: 18
                y: 2
                width: parent.width - 18
                height: 126
                radius: 7
                color: Theme.surfaceRaised
                border.width: 1
                border.color: timeline.eventIsFuture(modelData) ? timeline.neutralMark : timeline.gradeColor(modelData.grade)
                opacity: timeline.eventIsFuture(modelData) ? 0.82 : 1
            }
            Column {
                visible: timeline.variant === 0
                x: 28
                y: 16
                width: parent.width - 38
                spacing: 4
                Label {
                    width: parent.width
                    text: timeline.eventCaption(modelData)
                    color: timeline.eventIsReview(modelData) ? timeline.gradeColor(modelData.grade) : Theme.ink
                    font.family: Theme.monoFont
                    font.pixelSize: 14
                    font.bold: true
                    elide: Text.ElideRight
                }
                Label {
                    width: parent.width
                    text: timeline.dateTime(modelData.at)
                    color: Theme.inkMuted
                    font.family: Theme.monoFont
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }
            Label {
                visible: timeline.variant === 0 && timeline.eventIsReview(modelData)
                x: 28
                y: 94
                text: Math.round(Number(modelData.recallFraction || 0) * 100) + "% recall / " + Number(modelData.responseSeconds || 0).toFixed(1) + " s"
                color: Theme.inkMuted
                font.family: Theme.monoFont
                font.pixelSize: 12
            }

            Rectangle {
                visible: timeline.variant === 1
                x: 0
                y: 106
                width: parent.width
                height: 1
                color: timeline.spine
                opacity: 0.72
            }
            Canvas {
                id: trace
                visible: timeline.variant === 1
                x: 0
                y: 0
                width: timeline.eventStep + 40
                height: parent.height
                antialiasing: true
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    const next = index + 1 < timeline.events.length ? timeline.events[index + 1] : null
                    if (!timeline.eventIsReview(modelData) || !next || !timeline.eventIsReview(next))
                        return
                    ctx.strokeStyle = timeline.gradeColor(next.grade)
                    ctx.lineWidth = 2
                    ctx.beginPath()
                    ctx.moveTo(20, timeline.recallY(modelData) + 6)
                    ctx.lineTo(timeline.eventStep + 20, timeline.recallY(next) + 6)
                    ctx.stroke()
                }
                Component.onCompleted: requestPaint()
                Connections {
                    target: timeline
                    function onEventsChanged() { trace.requestPaint() }
                }
                Connections {
                    target: Theme
                    function onDarkChanged() { trace.requestPaint() }
                }
            }
            Rectangle {
                visible: timeline.variant === 1
                x: 14
                y: timeline.eventIsReview(modelData) ? timeline.recallY(modelData) : 100
                width: 12
                height: 12
                radius: 6
                color: timeline.eventIsFuture(modelData) ? Theme.transparent : timeline.gradeColor(modelData.grade)
                border.width: 2
                border.color: timeline.eventIsFuture(modelData) ? timeline.neutralMark : timeline.gradeColor(modelData.grade)
            }
            Label {
                visible: timeline.variant === 1
                x: 0
                y: 0
                width: parent.width
                text: timeline.eventCaption(modelData)
                color: timeline.eventIsReview(modelData) ? timeline.gradeColor(modelData.grade) : Theme.ink
                font.family: Theme.monoFont
                font.pixelSize: 14
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
            Label {
                visible: timeline.variant === 1
                x: 0
                y: 20
                width: parent.width
                text: timeline.eventIsReview(modelData) ? Math.round(Number(modelData.recallFraction || 0) * 100) + "% recall" : timeline.dateTime(modelData.at)
                color: Theme.inkMuted
                font.family: Theme.monoFont
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
            Label {
                visible: timeline.variant === 1
                x: 0
                y: 120
                width: parent.width
                text: timeline.dateTime(modelData.at)
                color: Theme.inkMuted
                font.family: Theme.monoFont
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
            }

            Rectangle {
                visible: timeline.variant === 2
                x: 0
                y: 64
                width: parent.width
                height: 3
                color: timeline.spine
            }
            Rectangle {
                visible: timeline.variant === 2
                x: parent.width / 2 - 7
                y: 58
                width: 14
                height: 14
                radius: 7
                color: timeline.eventIsFuture(modelData) ? Theme.ledgerSurface : timeline.eventIsReview(modelData) ? timeline.gradeColor(modelData.grade) : Theme.ledgerSurface
                border.width: 2
                border.color: timeline.eventIsReview(modelData) ? timeline.gradeColor(modelData.grade) : timeline.neutralMark
            }
            Rectangle {
                visible: timeline.variant === 2
                x: 10
                y: index % 2 === 0 ? 0 : 78
                width: parent.width - 20
                height: 52
                radius: 5
                color: Theme.surfaceRaised
                border.width: 1
                border.color: timeline.eventIsReview(modelData) ? timeline.gradeColor(modelData.grade) : Theme.rule
            }
            Label {
                visible: timeline.variant === 2
                x: 20
                y: index % 2 === 0 ? 9 : 87
                width: parent.width - 40
                text: timeline.eventCaption(modelData)
                color: timeline.eventIsReview(modelData) ? timeline.gradeColor(modelData.grade) : Theme.ink
                font.family: Theme.monoFont
                font.pixelSize: 14
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
            Label {
                visible: timeline.variant === 2
                x: 10
                y: index % 2 === 0 ? 34 : 112
                width: parent.width - 20
                text: timeline.dateTime(modelData.at)
                color: Theme.inkMuted
                font.family: Theme.monoFont
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
            }

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.NoButton
                ToolTip.visible: containsMouse
                ToolTip.text: timeline.eventDetails(modelData)
            }
        }
    }

    Label {
        visible: timeline.events.length === 0
        anchors.centerIn: reviewEvents
        text: timeline.loading ? "Loading history..." : "No recorded activity"
        color: Theme.inkMuted
        font.family: Theme.monoFont
        font.pixelSize: 11
    }

    WheelHandler {
        target: reviewEvents
        onWheel: function(event) {
            if (reviewEvents.contentWidth <= reviewEvents.width)
                return
            reviewEvents.contentX = Math.max(0, Math.min(reviewEvents.contentWidth - reviewEvents.width,
                reviewEvents.contentX - event.angleDelta.y * 0.7 - event.angleDelta.x * 0.7))
            event.accepted = true
        }
    }

    Keys.onPressed: function(event) {
        const step = Math.max(80, timeline.eventStep)
        switch (event.key) {
        case Qt.Key_Left:
            reviewEvents.contentX = Math.max(0, reviewEvents.contentX - step)
            event.accepted = true
            break
        case Qt.Key_Right:
            reviewEvents.contentX = Math.min(maxScroll(), reviewEvents.contentX + step)
            event.accepted = true
            break
        case Qt.Key_Home:
            reviewEvents.contentX = 0
            event.accepted = true
            break
        case Qt.Key_End:
            reviewEvents.positionViewAtEnd()
            event.accepted = true
            break
        }
    }
}
