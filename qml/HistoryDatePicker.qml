pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: picker

    property string initialStart: ""
    property string initialEnd: ""
    property string draftStart: ""
    property string draftEnd: ""
    property int monthYear: new Date().getFullYear()
    property int monthIndex: new Date().getMonth()
    property string validationError: ""

    signal applied(string startDate, string endDate)

    width: Math.min(382, Math.max(180, parent ? parent.width - 16 : 382))
    height: Math.min(480, Math.max(280, parent ? parent.height - 16 : 450))
    padding: 14
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { radius: 12; color: Theme.surfaceRaised; border.color: Theme.rule }

    function parseDay(value) {
        const text = String(value || "");
        if (!/^\d{4}-\d{2}-\d{2}$/.test(text))
            return NaN;
        const parts = text.split("-").map(Number);
        if (parts[0] < 1 || parts[0] > 9999)
            return NaN;
        const date = new Date(0);
        date.setUTCFullYear(parts[0], parts[1] - 1, parts[2]);
        date.setUTCHours(0, 0, 0, 0);
        return date.getUTCFullYear() === parts[0] && date.getUTCMonth() === parts[1] - 1 && date.getUTCDate() === parts[2]
            ? date.getTime() : NaN;
    }
    function dayKey(timestamp) {
        const date = new Date(timestamp);
        return String(date.getUTCFullYear()).padStart(4, "0") + "-" + String(date.getUTCMonth() + 1).padStart(2, "0") + "-" + String(date.getUTCDate()).padStart(2, "0");
    }
    function monthStart() {
        const date = new Date(0);
        date.setUTCFullYear(monthYear, monthIndex, 1);
        date.setUTCHours(0, 0, 0, 0);
        return date.getTime();
    }
    function openFor(startDate, endDate) {
        initialStart = startDate;
        initialEnd = endDate;
        draftStart = startDate;
        draftEnd = endDate;
        validationError = "";
        const focusDay = parseDay(startDate);
        const date = new Date(Number.isFinite(focusDay) ? focusDay : Date.now());
        monthYear = date.getUTCFullYear();
        monthIndex = date.getUTCMonth();
        open();
    }
    function selectDay(value) {
        if (!draftStart || (draftStart && draftEnd)) {
            draftStart = value;
            draftEnd = "";
        } else if (parseDay(value) < parseDay(draftStart)) {
            draftStart = value;
            draftEnd = "";
        } else {
            draftEnd = value;
        }
        validationError = "";
    }
    function applyDraft() {
        const start = parseDay(draftStart);
        const end = parseDay(draftEnd || draftStart);
        if (!Number.isFinite(start) || !Number.isFinite(end)) {
            validationError = "HISTORY_DATE_INVALID: Use valid dates in YYYY-MM-DD format.";
            return;
        }
        if (end < start) {
            validationError = "HISTORY_DATE_ORDER: The end date must be on or after the start date.";
            return;
        }
        applied(draftStart, draftEnd || draftStart);
        close();
    }

    contentItem: ColumnLayout {
        spacing: 10
        Label {
            text: "Choose review dates"
            color: Theme.ink
            font.family: Theme.uiFont
            font.pixelSize: 17
            font.weight: Font.DemiBold
            Layout.fillWidth: true
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            TextField {
                id: startField
                text: picker.draftStart
                placeholderText: "Start YYYY-MM-DD"
                font.family: Theme.monoFont
                font.pixelSize: 12
                Layout.fillWidth: true
                onTextEdited: picker.draftStart = text.trim()
            }
            Label { text: "to"; color: Theme.inkMuted; font.family: Theme.uiFont }
            TextField {
                id: endField
                text: picker.draftEnd
                placeholderText: "End YYYY-MM-DD"
                font.family: Theme.monoFont
                font.pixelSize: 12
                Layout.fillWidth: true
                onTextEdited: picker.draftEnd = text.trim()
            }
        }
        Label {
            text: "Choose one date, or enter a start and end date for an inclusive range."
            color: Theme.inkMuted
            font.family: Theme.uiFont
            font.pixelSize: 11
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: ["January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"][picker.monthIndex] + " " + picker.monthYear
                color: Theme.ink
                font.family: Theme.uiFont
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            GlyphButton { glyph: "arrow-left"; hint: "Previous month"; onClicked: picker.changeMonth(-1) }
            GlyphButton { glyph: "arrow-right"; hint: "Next month"; onClicked: picker.changeMonth(1) }
        }
        Row {
            Layout.fillWidth: true
            spacing: 4
            Repeater {
                model: ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]
                Label { required property string modelData; width: (picker.width - 24 - 6 * 4) / 7; text: modelData; horizontalAlignment: Text.AlignHCenter; color: Theme.inkMuted; font.family: Theme.monoFont; font.pixelSize: 10 }
            }
        }
        Grid {
            id: calendarGrid
            columns: 7
            rows: 6
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            clip: true
            spacing: 3
            Repeater {
                model: 42
                delegate: Button {
                    id: dayButton
                    required property int index
                    readonly property real timestamp: picker.monthStart() - ((new Date(picker.monthStart()).getUTCDay() + 6) % 7) * 86400000 + index * 86400000
                    readonly property string dateKey: picker.dayKey(timestamp)
                    readonly property bool inMonth: new Date(timestamp).getUTCMonth() === picker.monthIndex
                    readonly property bool selected: dateKey === picker.draftStart || dateKey === picker.draftEnd || (picker.parseDay(dateKey) > picker.parseDay(picker.draftStart) && picker.parseDay(dateKey) < picker.parseDay(picker.draftEnd))
                    implicitWidth: Math.max(30, (calendarGrid.width - 18) / 7)
                    implicitHeight: 31
                    text: new Date(timestamp).getUTCDate()
                    enabled: inMonth
                    opacity: inMonth ? 1 : 0.3
                    font.family: Theme.monoFont
                    font.pixelSize: 12
                    onClicked: picker.selectDay(dateKey)
                    ToolTip.visible: hovered
                    ToolTip.text: dateKey
                    background: Rectangle { radius: 5; color: dayButton.selected ? Theme.accentSoft : (dayButton.hovered ? Theme.ledgerSurfaceRaised : Theme.transparent); border.color: dayButton.selected ? Theme.accent : Theme.rule; border.width: dayButton.selected ? 2 : 1 }
                }
            }
        }
        Label {
            text: picker.validationError
            visible: text.length > 0
            color: Theme.error
            font.family: Theme.uiFont
            font.pixelSize: 12
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        RowLayout {
            Layout.fillWidth: true
            AppButton { text: "Cancel"; onClicked: picker.close() }
            Item { Layout.fillWidth: true }
            AppButton { text: "Apply"; primary: true; onClicked: picker.applyDraft() }
        }
    }
    function changeMonth(offset) {
        const cursor = new Date(0);
        cursor.setUTCFullYear(monthYear, monthIndex + offset, 1);
        cursor.setUTCHours(0, 0, 0, 0);
        monthYear = cursor.getUTCFullYear();
        monthIndex = cursor.getUTCMonth();
    }
    onOpened: startField.forceActiveFocus()
}
