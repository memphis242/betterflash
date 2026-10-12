import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: heatmap

    property var days: []
    property string today: ""
    property string firstDate: ""
    property bool compact: false
    property int selectedYear: new Date().getFullYear()
    property real selectedDate: todayMs

    readonly property real dayMs: 86400000
    readonly property real todayMs: validDay(today) ? parseDay(today) : parseDay(Qt.formatDate(new Date(), "yyyy-MM-dd"))
    readonly property int currentYear: new Date(todayMs).getUTCFullYear()
    readonly property int earliestYear: validDay(firstDate) ? Math.min(currentYear, new Date(parseDay(firstDate)).getUTCFullYear()) : currentYear
    readonly property int year: Math.max(earliestYear, Math.min(currentYear, selectedYear))
    readonly property real periodStart: compact ? monday(todayMs) - 84 * dayMs : yearStart(year)
    readonly property real periodEnd: compact ? todayMs : yearStart(year + 1) - dayMs
    readonly property real gridStart: monday(periodStart)
    readonly property int weekCount: compact ? 13 : Math.ceil(((periodEnd - gridStart) / dayMs + 1) / 7)
    readonly property int cellGap: 3
    readonly property int cellSize: Math.max(9, Math.min(compact ? 14 : 12, Math.floor((calendarViewport.width + cellGap) / Math.max(1, weekCount)) - cellGap, Math.floor((calendarViewport.height - 31) / 7) - cellGap))
    readonly property int cellStride: cellSize + cellGap
    readonly property real columnStride: (calendarViewport.contentWidth + cellGap) / weekCount
    readonly property real cellWidth: columnStride - cellGap
    readonly property var dayLookup: indexedDays()
    readonly property var cells: calendarDays()
    readonly property var monthLabels: calendarMonths()
    readonly property var monthNames: ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"]
    readonly property var activityColors: [Theme.activityLow, Theme.activityMedium, Theme.activityHigh, Theme.activityPeak]

    implicitHeight: compact ? 196 : 206
    implicitWidth: compact ? 300 : 800
    activeFocusOnTab: true
    clip: true
    Accessible.role: Accessible.Pane
    Accessible.name: compact ? "Review activity over the last 13 weeks" : "Review activity calendar for " + year
    Accessible.description: "Arrow keys move between dates. Page Up and Page Down change the year. Home returns to today."

    component YearButton: Item {
        id: yearButton
        required property bool available
        required property string glyph
        required property string hint
        signal activated()
        implicitWidth: 32
        implicitHeight: 32

        GlyphButton {
            id: yearControl
            anchors.fill: parent
            glyph: yearButton.glyph
            hint: yearButton.hint
            enabled: yearButton.available
            opacity: enabled ? 1 : 0.35
            ToolTip.visible: false
            onClicked: yearButton.activated()
        }
        HoverHandler { id: yearHover }
        ToolTip.visible: yearHover.hovered || yearControl.activeFocus
        ToolTip.delay: 250
        ToolTip.text: hint
    }

    function parseDay(value) {
        const parts = String(value || "").split("-");
        if (parts.length !== 3 || !/^\d{4}-\d{2}-\d{2}$/.test(value))
            return NaN;
        const date = new Date(0);
        date.setUTCFullYear(Number(parts[0]), Number(parts[1]) - 1, Number(parts[2]));
        date.setUTCHours(0, 0, 0, 0);
        if (date.getUTCFullYear() !== Number(parts[0]) || date.getUTCMonth() !== Number(parts[1]) - 1 || date.getUTCDate() !== Number(parts[2]))
            return NaN;
        return date.getTime();
    }

    function validDay(value) {
        return Number.isFinite(parseDay(value));
    }

    function dateKey(timestamp) {
        const date = new Date(timestamp);
        return String(date.getUTCFullYear()).padStart(4, "0") + "-" + String(date.getUTCMonth() + 1).padStart(2, "0") + "-" + String(date.getUTCDate()).padStart(2, "0");
    }

    function yearStart(value) {
        const date = new Date(0);
        date.setUTCFullYear(value, 0, 1);
        date.setUTCHours(0, 0, 0, 0);
        return date.getTime();
    }

    function monday(timestamp) {
        return timestamp - ((new Date(timestamp).getUTCDay() + 6) % 7) * dayMs;
    }

    function indexedDays() {
        const result = Object.create(null);
        for (const day of (days || [])) {
            if (day && validDay(day.date))
                result[day.date] = day;
        }
        return result;
    }

    function calendarDays() {
        const result = [];
        for (let i = 0; i < weekCount * 7; ++i) {
            const timestamp = gridStart + i * dayMs;
            result.push({
                timestamp: timestamp,
                date: dateKey(timestamp),
                week: Math.floor(i / 7),
                weekday: i % 7,
                inPeriod: timestamp >= periodStart && timestamp <= periodEnd
            });
        }
        return result;
    }

    function calendarMonths() {
        const labels = [];
        for (let timestamp = periodStart; timestamp <= periodEnd; timestamp += dayMs) {
            const date = new Date(timestamp);
            if (timestamp !== periodStart && date.getUTCDate() !== 1)
                continue;
            const week = Math.floor((timestamp - gridStart) / dayMs / 7);
            if (labels.length && (week - labels[labels.length - 1].week) * columnStride < 30)
                labels.pop();
            labels.push({
                week: week,
                text: monthNames[date.getUTCMonth()]
            });
        }
        return labels;
    }

    function fillColor(count) {
        if (count <= 0)
            return Theme.transparent;
        if (count <= 2)
            return activityColors[0];
        if (count <= 5)
            return activityColors[1];
        if (count <= 10)
            return activityColors[2];
        return activityColors[3];
    }

    function dateDescription(cell) {
        if (cell.timestamp > todayMs)
            return cell.date + " - Future date";
        const record = dayLookup[cell.date] || {};
        const count = Math.max(0, Number(record.count) || 0);
        let text = cell.date + " - " + count + (count === 1 ? " review" : " reviews");
        if (count > 0 && record.averageRecall !== null && Number.isFinite(Number(record.averageRecall)))
            text += " - " + Math.round(Number(record.averageRecall) * 100) + "% average recall";
        if (cell.timestamp === todayMs)
            text += " - Today";
        return text;
    }

    function scrollToDate(timestamp, animated) {
        const column = Math.floor((timestamp - gridStart) / dayMs / 7);
        const maximum = Math.max(0, calendarViewport.contentWidth - calendarViewport.width);
        const target = Math.max(0, Math.min(maximum, column * columnStride - calendarViewport.width / 2 + cellWidth / 2));
        scrollAnimation.stop();
        if (animated && !Theme.reducedMotion) {
            scrollAnimation.from = calendarViewport.contentX;
            scrollAnimation.to = target;
            scrollAnimation.start();
        } else {
            calendarViewport.contentX = target;
        }
    }

    function goToToday() {
        selectedYear = currentYear;
        selectedDate = todayMs;
        Qt.callLater(function () {
            heatmap.scrollToDate(heatmap.todayMs, true);
        });
    }

    function changeYear(value) {
        if (compact)
            return;
        selectedYear = Math.max(earliestYear, Math.min(currentYear, value));
        selectedDate = year === currentYear ? todayMs : yearStart(year);
        Qt.callLater(function () {
            heatmap.scrollToDate(heatmap.selectedDate, true);
        });
    }

    function moveDate(offset) {
        const lower = compact ? periodStart : yearStart(earliestYear);
        selectedDate = Math.max(lower, Math.min(todayMs, selectedDate + offset * dayMs));
        if (!compact)
            selectedYear = new Date(selectedDate).getUTCFullYear();
        Qt.callLater(function () {
            heatmap.scrollToDate(heatmap.selectedDate, false);
        });
    }

    onEarliestYearChanged: selectedYear = Math.max(earliestYear, Math.min(currentYear, selectedYear))
    onCurrentYearChanged: selectedYear = currentYear
    onYearChanged: {
        if (!compact && (selectedDate < yearStart(year) || selectedDate >= yearStart(year + 1)))
            selectedDate = year === currentYear ? todayMs : yearStart(year);
    }
    onCompactChanged: goToToday()
    Component.onCompleted: Qt.callLater(function () {
        heatmap.scrollToDate(heatmap.selectedDate, false);
    })

    Keys.onPressed: function (event) {
        switch (event.key) {
        case Qt.Key_Left:
            moveDate(-7);
            break;
        case Qt.Key_Right:
            moveDate(7);
            break;
        case Qt.Key_Up:
            moveDate(-1);
            break;
        case Qt.Key_Down:
            moveDate(1);
            break;
        case Qt.Key_PageUp:
            changeYear(year - 1);
            break;
        case Qt.Key_PageDown:
            changeYear(year + 1);
            break;
        case Qt.Key_Home:
            goToToday();
            break;
        default:
            return;
        }
        event.accepted = true;
    }

    NumberAnimation {
        id: scrollAnimation
        target: calendarViewport
        property: "contentX"
        duration: 180
        easing.type: Easing.OutCubic
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: heatmap.compact ? 24 : 34
            spacing: 6

            Label {
                Layout.fillWidth: true
                text: heatmap.compact ? "Last 13 weeks" : String(heatmap.year)
                color: Theme.ink
                font.family: Theme.monoFont
                font.pixelSize: heatmap.compact ? 13 : 16
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            YearButton {
                visible: !heatmap.compact
                glyph: "chevron-left-bold"
                available: heatmap.year > heatmap.earliestYear
                hint: available ? "Previous calendar year"
                    : heatmap.days.length ? "No review history before " + heatmap.earliestYear + " in this selection"
                    : "No review history yet in this selection"
                onActivated: heatmap.changeYear(heatmap.year - 1)
            }
            YearButton {
                visible: !heatmap.compact
                glyph: "chevron-right-bold"
                available: heatmap.year < heatmap.currentYear
                hint: available ? "Next calendar year"
                    : "Already showing " + heatmap.currentYear + ", the current year. Future years have no review history."
                onActivated: heatmap.changeYear(heatmap.year + 1)
            }
            AppButton {
                visible: !heatmap.compact
                text: "Today"
                hint: "Return to " + heatmap.dateKey(heatmap.todayMs)
                implicitHeight: 32
                onClicked: heatmap.goToToday()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 106
            spacing: 8

            Item {
                Layout.preferredWidth: 28
                Layout.fillHeight: true
                clip: true
                Repeater {
                    model: ["Mon", "", "Wed", "", "Fri", "", "Sun"]
                    Label {
                        required property string modelData
                        required property int index
                        y: 22 + index * heatmap.cellStride - calendarViewport.contentY
                        width: 28
                        height: heatmap.cellSize
                        text: modelData
                        color: Theme.inkMuted
                        font.family: Theme.monoFont
                        font.pixelSize: 10
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            Flickable {
                id: calendarViewport
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: Math.max(heatmap.compact ? 0 : width, heatmap.weekCount * heatmap.cellStride - heatmap.cellGap)
                contentHeight: 22 + 7 * heatmap.cellStride - heatmap.cellGap + 12
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.AutoFlickIfNeeded
                onMovementStarted: scrollAnimation.stop()
                ScrollBar.horizontal: ScrollBar {
                    policy: calendarViewport.contentWidth > calendarViewport.width ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                }
                ScrollBar.vertical: ScrollBar {
                    policy: calendarViewport.contentHeight > calendarViewport.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                }

                WheelHandler {
                    onWheel: function (event) {
                        if (calendarViewport.contentWidth <= calendarViewport.width)
                            return;
                        scrollAnimation.stop();
                        const delta = event.pixelDelta.x || event.pixelDelta.y || event.angleDelta.x / 3 || event.angleDelta.y / 3;
                        calendarViewport.contentX = Math.max(0, Math.min(calendarViewport.contentWidth - calendarViewport.width, calendarViewport.contentX - delta));
                        event.accepted = true;
                    }
                }

                Repeater {
                    model: heatmap.monthLabels
                    Label {
                        required property var modelData
                        x: modelData.week * heatmap.columnStride
                        width: Math.min(36, calendarViewport.contentWidth - x)
                        height: 18
                        text: modelData.text
                        color: Theme.inkMuted
                        font.family: Theme.monoFont
                        font.pixelSize: 10
                        elide: Text.ElideRight
                    }
                }

                Repeater {
                    model: heatmap.cells
                    Rectangle {
                        id: dayCell
                        required property var modelData
                        readonly property int count: modelData.timestamp <= heatmap.todayMs ? Math.max(0, Number((heatmap.dayLookup[modelData.date] || {}).count) || 0) : 0
                        readonly property bool keyboardSelected: heatmap.activeFocus && heatmap.selectedDate === modelData.timestamp
                        x: modelData.week * heatmap.columnStride
                        y: 22 + modelData.weekday * heatmap.cellStride
                        width: heatmap.cellWidth
                        height: heatmap.cellSize
                        radius: 2
                        visible: modelData.inPeriod
                        opacity: modelData.timestamp > heatmap.todayMs ? 0.4 : 1
                        color: heatmap.fillColor(count)
                        border.width: keyboardSelected || modelData.timestamp === heatmap.todayMs ? 2 : 1
                        border.color: keyboardSelected ? Theme.accent : modelData.timestamp === heatmap.todayMs ? Theme.ink : count ? color : Theme.rule
                        Accessible.role: Accessible.StaticText
                        Accessible.name: heatmap.dateDescription(modelData)
                        ToolTip.visible: dayHover.hovered || keyboardSelected
                        ToolTip.delay: keyboardSelected ? 0 : 250
                        ToolTip.text: heatmap.dateDescription(modelData)
                        HoverHandler {
                            id: dayHover
                        }
                        TapHandler {
                            onTapped: {
                                heatmap.selectedDate = dayCell.modelData.timestamp;
                                heatmap.forceActiveFocus();
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 18
            spacing: 5
            Label {
                text: "Reviews"
                color: Theme.inkMuted
                font.family: Theme.monoFont
                font.pixelSize: 10
            }
            Item {
                Layout.fillWidth: true
            }
            Label {
                text: "Less"
                color: Theme.inkMuted
                font.pixelSize: 10
            }
            Repeater {
                model: [0, 1, 3, 6, 11]
                Rectangle {
                    required property int modelData
                    Layout.preferredWidth: 11
                    Layout.preferredHeight: 11
                    radius: 2
                    color: heatmap.fillColor(modelData)
                    border.width: 1
                    border.color: modelData ? color : Theme.rule
                    ToolTip.visible: legendHover.hovered
                    ToolTip.text: {
                        switch (modelData) {
                        case 0:
                            return "No reviews";
                        case 1:
                            return "1 to 2 reviews in a day";
                        case 3:
                            return "3 to 5 reviews in a day";
                        case 6:
                            return "6 to 10 reviews in a day";
                        default:
                            return "11 or more reviews in a day";
                        }
                    }
                    HoverHandler {
                        id: legendHover
                    }
                }
            }
            Label {
                text: "More"
                color: Theme.inkMuted
                font.pixelSize: 10
            }
        }
    }
}
