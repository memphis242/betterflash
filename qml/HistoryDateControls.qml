pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: controls
    property string startDate: ""
    property string endDate: ""
    property bool currentWeek: true
    signal previousRequested
    signal nextRequested
    signal thisWeekRequested
    signal rangeSelected(string startDate, string endDate)
    spacing: 4
    implicitWidth: 360
    implicitHeight: 36

    GlyphButton {
        glyph: "arrow-left"
        hint: "Previous week"
        onClicked: controls.previousRequested()
    }
    Button {
        id: rangeButton
        text: controls.currentWeek ? (controls.startDate === controls.endDate ? controls.startDate + "  ·  This week" : controls.startDate + " - " + controls.endDate + "  ·  This week") : (controls.startDate === controls.endDate ? controls.startDate : controls.startDate + " - " + controls.endDate)
        font.family: Theme.monoFont
        font.pixelSize: 11
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        implicitHeight: 34
        ToolTip.visible: hovered
        ToolTip.text: "Choose a date or inclusive date range"
        onClicked: if (datePicker.visible)
            datePicker.close()
        else
            datePicker.openFor(controls.startDate, controls.endDate)
        background: Rectangle {
            radius: 6
            color: rangeButton.hovered ? Theme.accentSoft : Theme.transparent
            border.color: Theme.rule
            border.width: 1
        }
        contentItem: Label {
            text: rangeButton.text
            color: Theme.ink
            font: rangeButton.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }
    Button {
        id: thisWeekButton
        visible: !controls.currentWeek
        text: "This week"
        font.family: Theme.uiFont
        font.pixelSize: 11
        implicitHeight: 34
        ToolTip.visible: hovered
        ToolTip.text: "Return to the current Monday-Sunday week"
        onClicked: controls.thisWeekRequested()
        background: Rectangle {
            radius: 6
            color: thisWeekButton.hovered ? Theme.accentSoft : Theme.transparent
            border.color: Theme.rule
            border.width: 1
        }
        contentItem: Label {
            text: thisWeekButton.text
            color: Theme.ink
            font: thisWeekButton.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }
    GlyphButton {
        glyph: "arrow-right"
        hint: "Next week"
        onClicked: controls.nextRequested()
    }
    HistoryDatePicker {
        id: datePicker
        parent: rangeButton
        width: Math.min(382, Math.max(180, (Overlay.overlay ? Overlay.overlay.width : 382) - 16))
        x: {
            const overlay = Overlay.overlay;
            const anchorX = rangeButton.mapToItem(overlay, 0, 0).x;
            return Math.max(8, Math.min((overlay ? overlay.width : width) - width - 8, anchorX)) - anchorX;
        }
        y: {
            const overlay = Overlay.overlay;
            const top = rangeButton.mapToItem(overlay, 0, 0).y;
            const below = top + rangeButton.height + 6;
            const above = top - height - 6;
            const limit = overlay ? overlay.height : 860;
            if (below + height <= limit - 8)
                return below - top;
            if (above >= 8)
                return above - top;
            return Math.max(8, Math.min(limit - height - 8, below)) - top;
        }
        onApplied: (start, end) => controls.rangeSelected(start, end)
    }
    onVisibleChanged: if (!visible)
        datePicker.close()
}
