import QtQuick
import QtQuick.Controls

// A date-scaled review trace. The canvas stays the size of the viewport while
// the visible interval moves through the card's complete history.
Item {
    id: trace

    property var events: []
    property string historyStart: ""
    property string historyKey: ""
    property bool hasMore: false
    property bool loading: false
    property bool canLoadEarlier: true
    signal loadOlderRequested()

    implicitHeight: 320
    height: implicitHeight
    clip: true
    focus: true
    activeFocusOnTab: true
    Accessible.role: Accessible.ScrollBar
    Accessible.name: "Calendar scaled card review timeline"
    ToolTip.visible: hoveredIndex >= 0
    ToolTip.text: readoutText()
    ToolTip.delay: 350

    readonly property int plotTop: 92
    readonly property int plotBottom: Math.max(plotTop + 72, height - 76)
    readonly property int plotHeight: Math.max(1, plotBottom - plotTop)
    readonly property int plotLeft: 48
    readonly property int plotRight: 18
    readonly property int scrollbarHeight: 10
    readonly property real dayMs: 86400000
    readonly property var zoomNames: ["1 day", "3 days", "1 week", "1 month", "1 year", "All"]
    readonly property var zoomSpans: [dayMs, dayMs * 3, dayMs * 7, dayMs * 31, dayMs * 365]
    readonly property var outcomeColors: [Theme.recallMissed, Theme.recallPartial,
        Theme.recallHard, Theme.recallGood, Theme.recallEasy]

    property int zoomIndex: 3
    property real domainStartMs: 0
    property real domainEndMs: 0
    property real viewStartMs: 0
    property int hoveredIndex: -1
    property int selectedIndex: -1
    property string hoveredEventId: ""
    property string selectedEventId: ""
    property bool canvasReady: false
    property bool dragging: false
    property bool requestOutstanding: false

    function requestPaint() {
        if (canvasReady)
            plot.requestPaint()
    }

    function validDate(value) {
        const parsed = new Date(String(value || ""))
        return Number.isNaN(parsed.getTime()) ? 0 : parsed.getTime()
    }

    function nowMs() {
        for (let i = 0; i < events.length; ++i) {
            if (String(events[i].kind || "") === "now") {
                const at = validDate(events[i].at)
                if (at > 0)
                    return at
            }
        }
        return Date.now()
    }

    function earliestLoadedMs() {
        let result = 0
        for (let i = 0; i < events.length; ++i) {
            const kind = String(events[i].kind || "")
            if (kind !== "saved" && kind !== "created" && kind !== "modified" && kind !== "review")
                continue
            const at = validDate(events[i].at)
            if (at > 0 && (result === 0 || at < result))
                result = at
        }
        return result
    }

    function latestDueMs() {
        let result = nowMs()
        for (let i = 0; i < events.length; ++i) {
            if (String(events[i].kind || "") !== "due")
                continue
            const at = validDate(events[i].at)
            if (at > result)
                result = at
        }
        return result
    }

    function recomputeDomain(preserveView) {
        const loadedStart = earliestLoadedMs()
        const recordedStart = validDate(historyStart)
        let start = recordedStart > 0 ? recordedStart : loadedStart
        if (start <= 0)
            start = nowMs() - dayMs * 31
        const end = Math.max(nowMs(), latestDueMs()) + dayMs * 2
        if (end <= start)
            start = end - dayMs
        const prior = viewStartMs
        domainStartMs = start
        domainEndMs = end
        if (!preserveView || prior <= 0)
            viewStartMs = initialViewStart(end, start)
        else
            viewStartMs = clampViewStart(prior)
        requestPaint()
        maybeRequestOlder()
    }

    function initialViewStart(end, start) {
        const span = selectedSpan(start, end)
        const preferredEnd = Math.min(end, nowMs() + dayMs * 2)
        return clampViewStart(preferredEnd - span)
    }

    function selectedSpan(start, end) {
        if (zoomIndex >= zoomSpans.length)
            return Math.max(dayMs, end - start)
        return Math.min(Math.max(dayMs, end - start), zoomSpans[Math.max(0, zoomIndex)])
    }

    function visibleSpanMs() {
        return selectedSpan(domainStartMs, domainEndMs)
    }

    function clampViewStart(value) {
        const span = visibleSpanMs()
        const maximum = Math.max(domainStartMs, domainEndMs - span)
        return Math.max(domainStartMs, Math.min(maximum, Number(value)))
    }

    function setViewStart(value) {
        viewStartMs = clampViewStart(value)
        maybeRequestOlder()
        requestPaint()
    }

    function timeToX(value) {
        const span = visibleSpanMs()
        if (span <= 0)
            return plotLeft
        return plotLeft + (validDate(value) - viewStartMs) / span * plotWidth()
    }

    function plotWidth() { return Math.max(1, width - plotLeft - plotRight) }
    function xToTime(value) { return viewStartMs + (value - plotLeft) / plotWidth() * visibleSpanMs() }

    function gradeColor(grade) {
        const value = Number(grade)
        return value >= 0 && value < outcomeColors.length ? outcomeColors[value] : Theme.inkMuted
    }

    function gradeLabel(grade) {
        switch (Number(grade)) {
        case 0: return "Missed"
        case 1: return "Partial"
        case 2: return "Hard"
        case 3: return "Good"
        case 4: return "Easy"
        default: return "Unreviewed"
        }
    }

    function eventLabel(event) {
        const kind = String(event.kind || "")
        if (kind === "saved" || kind === "created") return "First saved"
        if (kind === "modified") return "Edited"
        if (kind === "due") return event.label ? String(event.label) : "Next review"
        if (kind === "now") return "Now"
        return gradeLabel(event.grade)
    }

    function dateTime(value) {
        const at = validDate(value)
        return at > 0 ? Qt.formatDateTime(new Date(at), "yyyy-MM-dd HH:mm") : "Unknown time"
    }

    function eventText(event) {
        const parts = [eventLabel(event), dateTime(event.at)]
        if (String(event.kind || "") === "review") {
            parts.push(Math.round(Number(event.recallFraction || 0) * 100) + "% recall")
            parts.push(Number(event.responseSeconds || 0).toFixed(1) + " s")
            if (event.variantKey)
                parts.push(String(event.variantKey))
        }
        if (String(event.kind || "") === "saved" || String(event.kind || "") === "created")
            parts[0] = "Saved (recorded; original creation may be earlier)"
        return parts.join("  |  ")
    }

    function eventIdentity(event) {
        return String(event.id || (String(event.kind || "") + "|" + String(event.at || "") + "|" + String(event.variantKey || "")))
    }

    function readoutText() {
        const active = hoveredIndex >= 0 ? hoveredIndex : selectedIndex
        if (active < 0 || active >= events.length)
            return "Select a point to inspect its review result"
        const anchor = pointAt(active)
        const nearby = []
        for (let i = 0; i < events.length; ++i) {
            const point = pointAt(i)
            if (point.x >= 0 && Math.hypot(point.x - anchor.x, point.y - anchor.y) <= 8)
                nearby.push(eventText(events[i]))
        }
        return nearby.join("\n")
    }

    function maybeRequestOlder() {
        const earliest = earliestLoadedMs()
        if (!visible || !canLoadEarlier || !hasMore || loading || requestOutstanding || earliest <= 0 || viewStartMs > earliest)
            return
        requestOutstanding = true
        loadOlderRequested()
        releaseRequestTimer.restart()
    }

    function reconcileSelection() {
        const findId = function(id) {
            if (!id) return -1
            for (let i = 0; i < events.length; ++i)
                if (eventIdentity(events[i]) === id) return i
            return -1
        }
        hoveredIndex = findId(hoveredEventId)
        selectedIndex = findId(selectedEventId)
        requestPaint()
    }

    function setZoom(index, anchorX) {
        let next = Math.max(0, Math.min(zoomSpans.length, Number(index)))
        if (next < zoomSpans.length && zoomSpans[next] >= domainEndMs - domainStartMs)
            next = zoomSpans.length
        const anchor = anchorX === undefined ? plotLeft + plotWidth() / 2 : anchorX
        const anchorTime = xToTime(anchor)
        zoomIndex = next
        const nextSpan = visibleSpanMs()
        const ratio = Math.max(0, Math.min(1, (anchor - plotLeft) / plotWidth()))
        setViewStart(anchorTime - nextSpan * ratio)
    }

    function zoomBy(delta, anchorX) {
        let next = zoomIndex + delta
        if (delta < 0) {
            const span = visibleSpanMs()
            while (next > 0 && zoomSpans[next] >= span) --next
        }
        setZoom(next, anchorX)
    }

    function showRecent(index) {
        setZoom(index)
        setViewStart(nowMs() - visibleSpanMs() * 0.9)
    }

    function showToday() {
        setViewStart(nowMs() - visibleSpanMs() / 2)
    }

    function recallY(fraction) {
        const value = Math.max(0, Math.min(1, Number(fraction || 0)))
        return plotBottom - 34 - value * Math.max(1, plotHeight - 54)
    }

    function tickMode() {
        const span = visibleSpanMs()
        if (span <= dayMs * 2) return "hour"
        if (span <= dayMs * 14) return "day"
        if (span <= dayMs * 100) return "week"
        if (span <= dayMs * 730) return "month"
        return "year"
    }

    function floorTick(date, mode) {
        const value = new Date(date.getTime())
        if (mode === "hour") {
            value.setMinutes(0, 0, 0)
        } else if (mode === "day") {
            value.setHours(0, 0, 0, 0)
        } else if (mode === "week") {
            value.setHours(0, 0, 0, 0)
            const weekday = value.getDay()
            value.setDate(value.getDate() - weekday)
        } else if (mode === "month") {
            value.setDate(1)
            value.setHours(0, 0, 0, 0)
        } else {
            value.setMonth(0, 1)
            value.setHours(0, 0, 0, 0)
        }
        return value
    }

    function nextTick(date, mode) {
        const value = new Date(date.getTime())
        if (mode === "hour") value.setHours(value.getHours() + 1)
        else if (mode === "day") value.setDate(value.getDate() + 1)
        else if (mode === "week") value.setDate(value.getDate() + 7)
        else if (mode === "month") value.setMonth(value.getMonth() + 1)
        else value.setFullYear(value.getFullYear() + 1)
        return value
    }

    function tickLabel(date, mode) {
        if (mode === "hour") return Qt.formatTime(date, "HH:mm")
        if (mode === "year") return Qt.formatDate(date, "yyyy")
        if (mode === "month") return Qt.formatDate(date, "yyyy-MM")
        return Qt.formatDate(date, "yyyy-MM-dd")
    }

    function tickOrdinal(date, mode) {
        switch (mode) {
        case "hour": return Math.floor(date.getTime() / 3600000)
        case "month": return date.getFullYear() * 12 + date.getMonth()
        case "year": return date.getFullYear()
        case "week": return Math.floor((Date.UTC(date.getFullYear(), date.getMonth(), date.getDate()) / dayMs - 3) / 7)
        default: return Math.floor(Date.UTC(date.getFullYear(), date.getMonth(), date.getDate()) / dayMs)
        }
    }

    function pointAt(index) {
        if (index < 0 || index >= events.length) return Qt.point(-1, -1)
        const event = events[index]
        const at = validDate(event.at)
        if (at <= 0) return Qt.point(-1, -1)
        const kind = String(event.kind || "")
        const fraction = Math.max(0, Math.min(1, Number(event.recallFraction || 0)))
        return Qt.point(timeToX(event.at), kind === "review" ? recallY(fraction) : plotBottom - 13)
    }

    function nearestIndex(x, y) {
        let result = -1
        let distance = 15
        for (let i = 0; i < events.length; ++i) {
            const point = pointAt(i)
            if (point.x < plotLeft - 2 || point.x > width - plotRight + 2) continue
            const kind = String(events[i].kind || "")
            const isDateLine = kind === "due" || kind === "now"
            const current = isDateLine && y >= plotTop && y <= plotBottom - 11
                ? Math.abs(point.x - x) : Math.hypot(point.x - x, point.y - y)
            if (current <= distance) { distance = current; result = i }
        }
        return result
    }

    function setNearest(x, y, selected) {
        const index = nearestIndex(x, y)
        hoveredIndex = index
        hoveredEventId = index >= 0 ? eventIdentity(events[index]) : ""
        if (selected && index >= 0) {
            selectedIndex = index
            selectedEventId = eventIdentity(events[index])
        }
        requestPaint()
    }

    function resetSelection() { hoveredIndex = -1; hoveredEventId = ""; requestPaint() }

    onEventsChanged: {
        reconcileSelection()
        recomputeDomain(true)
    }
    onHistoryStartChanged: recomputeDomain(true)
    onHistoryKeyChanged: {
        selectedIndex = -1
        selectedEventId = ""
        hoveredIndex = -1
        hoveredEventId = ""
        recomputeDomain(false)
    }
    onLoadingChanged: if (!loading) {
        requestOutstanding = false
        Qt.callLater(maybeRequestOlder)
    }
    onHasMoreChanged: { requestPaint(); Qt.callLater(maybeRequestOlder) }
    onCanLoadEarlierChanged: if (canLoadEarlier) Qt.callLater(maybeRequestOlder)
    onVisibleChanged: if (visible) { requestPaint(); Qt.callLater(maybeRequestOlder) }
    onZoomIndexChanged: {
        requestPaint()
        if (zoomPicker)
            zoomPicker.currentIndex = zoomIndex
    }
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    Component.onCompleted: {
        canvasReady = true
        recomputeDomain(false)
    }

    Connections {
        target: Theme
        function onDarkChanged() { trace.requestPaint() }
    }

    Timer {
        id: releaseRequestTimer
        interval: 800
        repeat: false
        onTriggered: if (!trace.loading) trace.requestOutstanding = false
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.ledgerSurface
        border.width: 1
        border.color: Theme.ledgerRule
        radius: 12
    }

    Label {
        x: 18
        y: 12
        width: Math.max(0, zoomControls.x - x - 12)
        text: "Recall trace"
        elide: Text.ElideRight
        color: Theme.inkMuted
        font.family: Theme.monoFont
        font.pixelSize: 12
        font.bold: true
        font.letterSpacing: 1.1
    }

    Label {
        x: 18
        y: 53
        width: Math.max(0, (loadEarlier.visible ? loadEarlier.x : trace.width - 18) - x - 12)
        text: Qt.formatDate(new Date(trace.viewStartMs), "yyyy-MM-dd") + " to "
            + Qt.formatDate(new Date(trace.viewStartMs + trace.visibleSpanMs()), "yyyy-MM-dd")
        color: Theme.inkMuted
        font.family: Theme.monoFont
        font.pixelSize: 11
        elide: Text.ElideRight
    }

    Row {
        id: zoomControls
        anchors.right: parent.right
        anchors.rightMargin: 14
        y: 9
        spacing: 4
        AppButton {
            text: "Today"
            hint: "Return to today without changing zoom (T when the timeline is focused)"
            font.pixelSize: 11
            onClicked: trace.showToday()
        }
        AppButton {
            text: "−"
            hint: "Zoom out"
            font.pixelSize: 16
            implicitWidth: 28
            onClicked: trace.zoomBy(1, trace.plotLeft + trace.plotWidth() / 2)
        }
        ComboBox {
            id: zoomPicker
            width: 104
            height: 32
            model: trace.zoomNames
            currentIndex: trace.zoomIndex
            font.family: Theme.monoFont
            font.pixelSize: 11
            onActivated: function(index) { trace.showRecent(index) }
            Accessible.name: "Visible time span"
            ToolTip.visible: hovered
            ToolTip.text: "Choose the time span shown by the calendar"
            background: Rectangle {
                color: Theme.surfaceRaised
                border.color: Theme.ledgerRule
                radius: 5
            }
            contentItem: Label {
                text: zoomPicker.displayText
                color: Theme.ink
                font: zoomPicker.font
                verticalAlignment: Text.AlignVCenter
                leftPadding: 8
            }
        }
        AppButton {
            text: "+"
            hint: "Zoom in"
            font.pixelSize: 16
            implicitWidth: 28
            onClicked: trace.zoomBy(-1, trace.plotLeft + trace.plotWidth() / 2)
        }
    }

    AppButton {
        id: loadEarlier
        visible: trace.hasMore
        x: Math.max(18, trace.width - width - 14)
        y: 40
        text: trace.loading ? "Loading earlier history" : "Earlier history"
        hint: trace.loading ? "Loading earlier review history" : "Load earlier review history"
        enabled: !trace.loading && trace.canLoadEarlier
        font.pixelSize: 11
        onClicked: {
            if (!trace.requestOutstanding) {
                trace.requestOutstanding = true
                trace.loadOlderRequested()
            }
        }
    }

    Canvas {
        id: plot
        x: 0
        y: 0
        width: parent.width
        height: parent.height
        antialiasing: true
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const axisY = trace.plotBottom - 13
            const mode = trace.tickMode()
            const first = trace.floorTick(new Date(trace.viewStartMs), mode)
            ctx.font = "11px 'IBM Plex Mono'"
            ctx.textAlign = "center"
            let tick = first
            let guard = 0
            const tickStepMs = mode === "hour" ? 3600000 : mode === "day" ? trace.dayMs : mode === "week" ? trace.dayMs * 7 : mode === "month" ? trace.dayMs * 30 : trace.dayMs * 365
            const tickStride = Math.max(1, Math.ceil((trace.visibleSpanMs() / tickStepMs) / Math.max(1, trace.plotWidth() / 90)))
            while (tick.getTime() <= trace.viewStartMs + trace.visibleSpanMs() && guard++ < 1000) {
                const x = trace.timeToX(tick.toISOString())
                if (trace.tickOrdinal(tick, mode) % tickStride === 0 && x >= trace.plotLeft - 1 && x <= trace.width - trace.plotRight + 1) {
                    ctx.strokeStyle = Theme.ledgerRule
                    ctx.globalAlpha = 0.45
                    ctx.lineWidth = 1
                    ctx.beginPath()
                    ctx.moveTo(x, trace.plotTop)
                    ctx.lineTo(x, axisY)
                    ctx.stroke()
                    ctx.globalAlpha = 1
                    ctx.fillStyle = Theme.inkMuted
                    ctx.fillText(trace.tickLabel(tick, mode), x, axisY + 26)
                }
                tick = trace.nextTick(tick, mode)
            }

            ctx.strokeStyle = Theme.ledgerRule
            ctx.lineWidth = 1
            ctx.beginPath()
            ctx.moveTo(trace.plotLeft, axisY)
            ctx.lineTo(trace.width - trace.plotRight, axisY)
            ctx.stroke()

            ctx.font = "10px 'IBM Plex Mono'"
            ctx.textAlign = "right"
            ctx.fillStyle = Theme.inkMuted
            ctx.fillText("100%", trace.plotLeft - 8, trace.recallY(1) + 4)
            ctx.fillText("50%", trace.plotLeft - 8, trace.recallY(0.5) + 4)
            ctx.fillText("0%", trace.plotLeft - 8, trace.recallY(0) + 4)
            for (const fraction of [0, 0.5, 1]) {
                ctx.strokeStyle = Theme.ledgerRule
                ctx.globalAlpha = 0.35
                ctx.beginPath()
                ctx.moveTo(trace.plotLeft, trace.recallY(fraction))
                ctx.lineTo(trace.width - trace.plotRight, trace.recallY(fraction))
                ctx.stroke()
            }
            ctx.globalAlpha = 1

            if (trace.hasMore && trace.earliestLoadedMs() > trace.viewStartMs) {
                const unknownEnd = Math.min(trace.width - trace.plotRight, trace.timeToX(new Date(trace.earliestLoadedMs()).toISOString()))
                if (unknownEnd > trace.plotLeft) {
                    ctx.fillStyle = Theme.surfaceRaised
                    ctx.globalAlpha = 0.58
                    ctx.fillRect(trace.plotLeft, trace.plotTop, unknownEnd - trace.plotLeft, trace.plotBottom - trace.plotTop)
                    ctx.globalAlpha = 1
                    ctx.fillStyle = Theme.inkMuted
                    ctx.font = "10px 'IBM Plex Mono'"
                    ctx.textAlign = "center"
                    ctx.fillText("Earlier history not loaded", (trace.plotLeft + unknownEnd) / 2, trace.plotTop + 18)
                }
            }

            ctx.save()
            ctx.beginPath()
            ctx.rect(trace.plotLeft, trace.plotTop, trace.plotWidth(), trace.plotBottom - trace.plotTop)
            ctx.clip()
            let previousReview = null
            for (let i = 0; i < trace.events.length; ++i) {
                const event = trace.events[i]
                const at = trace.validDate(event.at)
                if (at <= 0) continue
                const point = trace.pointAt(i)
                const kind = String(event.kind || "")
                if (kind === "review") {
                    const key = String(event.variantKey || "")
                    if (previousReview && key.length > 0 && String(previousReview.event.variantKey || "") === key) {
                        ctx.strokeStyle = trace.gradeColor(event.grade)
                        ctx.globalAlpha = 0.72
                        ctx.lineWidth = 2
                        ctx.beginPath()
                        ctx.moveTo(previousReview.point.x, previousReview.point.y)
                        ctx.lineTo(point.x, point.y)
                        ctx.stroke()
                        ctx.globalAlpha = 1
                    }
                    previousReview = { event: event, point: point }
                } else if (kind !== "now") {
                    previousReview = null
                }
            }

            for (let i = 0; i < trace.events.length; ++i) {
                const event = trace.events[i]
                const point = trace.pointAt(i)
                if (point.x < trace.plotLeft - 5 || point.x > trace.width - trace.plotRight + 5) continue
                const kind = String(event.kind || "")
                const focused = i === trace.hoveredIndex || i === trace.selectedIndex
                if (kind === "review") {
                    ctx.fillStyle = trace.gradeColor(event.grade)
                    ctx.strokeStyle = trace.gradeColor(event.grade)
                    ctx.lineWidth = focused ? 3 : 2
                    ctx.beginPath()
                    ctx.arc(point.x, point.y, focused ? 6 : 5, 0, Math.PI * 2)
                    ctx.fill()
                    if (focused) { ctx.strokeStyle = Theme.ink; ctx.stroke() }
                } else if (kind === "due" || kind === "now") {
                    ctx.strokeStyle = kind === "due" ? Theme.timelineDue : Theme.accent
                    ctx.lineWidth = focused ? 3 : 2
                    ctx.beginPath()
                    ctx.moveTo(point.x, trace.plotTop - 5)
                    ctx.lineTo(point.x, axisY + 2)
                    ctx.stroke()
                } else {
                    ctx.fillStyle = Theme.inkMuted
                    ctx.strokeStyle = Theme.inkMuted
                    ctx.lineWidth = 1
                    ctx.beginPath()
                    ctx.arc(point.x, axisY, 4, 0, Math.PI * 2)
                    ctx.fill()
                }
            }

            const active = trace.selectedIndex >= 0 ? trace.selectedIndex : trace.hoveredIndex
            if (active >= 0 && active < trace.events.length
                    && trace.events[active].kind !== "due" && trace.events[active].kind !== "now") {
                const activePoint = trace.pointAt(active)
                ctx.strokeStyle = Theme.accent
                ctx.globalAlpha = 0.7
                ctx.beginPath()
                ctx.moveTo(activePoint.x, trace.plotTop)
                ctx.lineTo(activePoint.x, trace.plotBottom)
                ctx.stroke()
                ctx.globalAlpha = 1
            }
            ctx.restore()
        }
    }

    Label {
        x: 18
        y: trace.plotBottom + 32
        width: Math.max(0, parent.width - 36)
        text: trace.readoutText()
        color: trace.selectedIndex >= 0 && trace.selectedIndex < trace.events.length
            ? (trace.events[trace.selectedIndex].kind === "review"
                ? trace.gradeColor(trace.events[trace.selectedIndex].grade)
                : trace.events[trace.selectedIndex].kind === "due" ? Theme.timelineDue : Theme.inkMuted)
            : Theme.inkMuted
        font.family: Theme.monoFont
        font.pixelSize: 11
        elide: Text.ElideRight
    }

    Rectangle {
        id: scrollbarTrack
        x: trace.plotLeft
        y: trace.height - trace.scrollbarHeight - 13
        width: Math.max(1, trace.width - trace.plotLeft - trace.plotRight)
        height: trace.scrollbarHeight
        radius: height / 2
        color: Theme.surfaceRaised
        border.color: Theme.ledgerRule
        border.width: 1
        Rectangle {
            id: scrollbarHandle
            x: trace.domainEndMs > trace.domainStartMs ? (trace.viewStartMs - trace.domainStartMs) / Math.max(1, trace.domainEndMs - trace.domainStartMs - trace.visibleSpanMs()) * Math.max(0, parent.width - width) : 0
            y: 1
            width: Math.min(parent.width, Math.max(28, parent.width * Math.min(1, trace.visibleSpanMs() / Math.max(1, trace.domainEndMs - trace.domainStartMs))))
            height: parent.height - 2
            radius: height / 2
            color: Theme.ledgerRule
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.SizeHorCursor
                property real startX: 0
                property real startView: 0
                onPressed: function(mouse) {
                    trace.forceActiveFocus()
                    const position = scrollbarHandle.mapToItem(scrollbarTrack, mouse.x, mouse.y)
                    startX = position.x
                    startView = trace.viewStartMs
                }
                onPositionChanged: function(mouse) {
                    if (!pressed) return
                    const usable = Math.max(1, scrollbarTrack.width - scrollbarHandle.width)
                    const range = Math.max(1, trace.domainEndMs - trace.domainStartMs - trace.visibleSpanMs())
                    const position = scrollbarHandle.mapToItem(scrollbarTrack, mouse.x, mouse.y)
                    trace.setViewStart(startView + (position.x - startX) / usable * range)
                }
            }
        }
        MouseArea {
            anchors.fill: parent
            z: -1
            onClicked: function(mouse) {
                trace.forceActiveFocus()
                const ratio = Math.max(0, Math.min(1, mouse.x / parent.width))
                trace.setViewStart(trace.domainStartMs + ratio * Math.max(0, trace.domainEndMs - trace.domainStartMs - trace.visibleSpanMs()))
            }
        }
    }

    MouseArea {
        id: plotMouse
        x: trace.plotLeft
        y: trace.plotTop - 12
        width: Math.max(1, trace.plotWidth())
        height: Math.max(1, trace.plotBottom - trace.plotTop + 18)
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        property real pressX: 0
        property real pressView: 0
        onPressed: function(mouse) {
            trace.forceActiveFocus()
            pressX = mouse.x
            pressView = trace.viewStartMs
            trace.dragging = false
        }
        onPositionChanged: function(mouse) {
            if (pressed && Math.abs(mouse.x - pressX) > 3) {
                trace.dragging = true
                trace.setViewStart(pressView - (mouse.x - pressX) / trace.plotWidth() * trace.visibleSpanMs())
            } else if (!pressed) {
                trace.setNearest(mouse.x + trace.plotLeft, mouse.y + trace.plotTop - 12, false)
            }
        }
        onReleased: function(mouse) {
            if (!trace.dragging)
                trace.setNearest(mouse.x + trace.plotLeft, mouse.y + trace.plotTop - 12, true)
            trace.dragging = false
        }
        onExited: if (!trace.dragging) trace.resetSelection()
        onWheel: function(wheel) {
            const horizontal = wheel.angleDelta.x !== 0 ? wheel.angleDelta.x : wheel.pixelDelta.x
            const vertical = wheel.angleDelta.y !== 0 ? wheel.angleDelta.y : wheel.pixelDelta.y
            if (wheel.modifiers & Qt.ControlModifier)
                trace.zoomBy(vertical > 0 ? -1 : 1, wheel.x + trace.plotLeft)
            else
                trace.setViewStart(trace.viewStartMs - (horizontal !== 0 ? horizontal : vertical) / 120 * trace.visibleSpanMs() * 0.18)
            wheel.accepted = true
        }
    }

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_T && event.modifiers === Qt.NoModifier) {
            trace.showToday()
            event.accepted = true
        } else if (event.key === Qt.Key_Home) {
            trace.setViewStart(trace.domainStartMs)
            event.accepted = true
        } else if (event.key === Qt.Key_End) {
            trace.setViewStart(trace.domainEndMs - trace.visibleSpanMs())
            event.accepted = true
        } else if (event.key === Qt.Key_PageUp || event.key === Qt.Key_PageDown) {
            trace.setViewStart(trace.viewStartMs + (event.key === Qt.Key_PageDown ? 1 : -1) * trace.visibleSpanMs() * 0.8)
            event.accepted = true
        } else if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
            const direction = event.key === Qt.Key_Right ? 1 : -1
            const next = Math.max(0, Math.min(trace.events.length - 1, (trace.selectedIndex >= 0 ? trace.selectedIndex : direction > 0 ? -1 : trace.events.length) + direction))
            if (next >= 0 && next < trace.events.length) {
                trace.selectedIndex = next
                trace.selectedEventId = trace.eventIdentity(trace.events[next])
                const point = trace.pointAt(next)
                if (point.x < trace.plotLeft || point.x > trace.width - trace.plotRight)
                    trace.setViewStart(trace.validDate(trace.events[next].at) - trace.visibleSpanMs() / 2)
                trace.requestPaint()
            }
            event.accepted = true
        } else if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal) {
            trace.zoomBy(-1, trace.plotLeft + trace.plotWidth() / 2)
            event.accepted = true
        } else if (event.key === Qt.Key_Minus) {
            trace.zoomBy(1, trace.plotLeft + trace.plotWidth() / 2)
            event.accepted = true
        }
    }
}
