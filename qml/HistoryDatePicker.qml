pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: picker

    property string draftStart: ""
    property string draftEnd: ""
    property int monthYear: new Date().getFullYear()
    property int monthIndex: new Date().getMonth()
    property string validationError: ""
    onDraftStartChanged: startField.text = draftStart
    onDraftEndChanged: endField.text = draftEnd

    signal applied(string startDate, string endDate)

    width: Math.min(382, Math.max(180, Overlay.overlay ? Overlay.overlay.width - 16 : 382))
    height: Math.min(520, Math.max(0, Overlay.overlay ? Overlay.overlay.height - 16 : 520))
    padding: 14
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
    background: Rectangle {
        radius: 12
        color: Theme.surfaceRaised
        border.color: Theme.rule
    }

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
        return date.getUTCFullYear() === parts[0] && date.getUTCMonth() === parts[1] - 1 && date.getUTCDate() === parts[2] ? date.getTime() : NaN;
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
        const startKey = draftStart.trim();
        const endKey = draftEnd.trim() || startKey;
        const start = parseDay(startKey);
        const end = parseDay(endKey);
        if (!Number.isFinite(start) || !Number.isFinite(end)) {
            validationError = "HISTORY_DATE_INVALID: Use valid dates in YYYY-MM-DD format.";
            return;
        }
        if (end < start) {
            validationError = "HISTORY_DATE_ORDER: The end date must be on or after the start date.";
            return;
        }
        applied(startKey, endKey);
        close();
    }

    contentItem: ColumnLayout {
        spacing: 10
        ScrollView {
            id: pickerScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                width: pickerScroll.availableWidth
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
                        placeholderText: "Start YYYY-MM-DD"
                        font.family: Theme.monoFont
                        font.pixelSize: 12
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Accessible.name: "Start date in YYYY-MM-DD format"
                        onTextEdited: {
                            picker.draftStart = text;
                            picker.validationError = "";
                        }
                    }
                    Label {
                        text: "to"
                        color: Theme.inkMuted
                        font.family: Theme.uiFont
                    }
                    TextField {
                        id: endField
                        placeholderText: "End YYYY-MM-DD"
                        font.family: Theme.monoFont
                        font.pixelSize: 12
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Accessible.name: "End date in YYYY-MM-DD format. Leave empty for a single day."
                        onTextEdited: {
                            picker.draftEnd = text;
                            picker.validationError = "";
                        }
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
                    GlyphButton {
                        glyph: "arrow-left"
                        hint: "Previous month"
                        enabled: picker.monthYear > 1 || picker.monthIndex > 0
                        onClicked: picker.changeMonth(-1)
                    }
                    GlyphButton {
                        glyph: "arrow-right"
                        hint: "Next month"
                        enabled: picker.monthYear < 9999 || picker.monthIndex < 11
                        onClicked: picker.changeMonth(1)
                    }
                }
                Row {
                    id: weekdays
                    Layout.fillWidth: true
                    spacing: 3
                    Repeater {
                        model: ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]
                        Label {
                            required property string modelData
                            width: (weekdays.width - 6 * weekdays.spacing) / 7
                            text: modelData
                            horizontalAlignment: Text.AlignHCenter
                            color: Theme.inkMuted
                            font.family: Theme.monoFont
                            font.pixelSize: 10
                        }
                    }
                }
                Grid {
                    id: calendarGrid
                    columns: 7
                    rows: 6
                    Layout.fillWidth: true
                    Layout.preferredHeight: 6 * 31 + 5 * spacing
                    Layout.minimumHeight: Layout.preferredHeight
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
                            Accessible.name: dateKey
                            Accessible.selected: selected
                            enabled: inMonth
                            opacity: inMonth ? 1 : 0.3
                            font.family: Theme.monoFont
                            font.pixelSize: 12
                            onClicked: picker.selectDay(dateKey)
                            ToolTip.visible: hovered
                            ToolTip.text: dateKey
                            background: Rectangle {
                                radius: 5
                                color: dayButton.selected ? Theme.accentSoft : (dayButton.enabled && dayButton.hovered ? Theme.ledgerSurfaceRaised : Theme.transparent)
                                border.color: dayButton.selected || dayButton.activeFocus ? Theme.accent : Theme.rule
                                border.width: dayButton.selected || dayButton.activeFocus ? 2 : 1
                            }
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
            }
        }
        RowLayout {
            Layout.fillWidth: true
            AppButton {
                text: "Cancel"
                onClicked: picker.close()
            }
            Item {
                Layout.fillWidth: true
            }
            AppButton {
                text: "Apply"
                primary: true
                onClicked: picker.applyDraft()
            }
        }
    }
    function changeMonth(offset) {
        const cursor = new Date(0);
        cursor.setUTCFullYear(monthYear, monthIndex + offset, 1);
        cursor.setUTCHours(0, 0, 0, 0);
        if (cursor.getUTCFullYear() < 1 || cursor.getUTCFullYear() > 9999)
            return;
        monthYear = cursor.getUTCFullYear();
        monthIndex = cursor.getUTCMonth();
    }
    onOpened: {
        startField.text = draftStart;
        endField.text = draftEnd;
        startField.forceActiveFocus();
    }
}
