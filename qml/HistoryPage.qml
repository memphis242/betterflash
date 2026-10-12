pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore

Item {
    id: historyPage
    required property var ui
    readonly property bool narrow: width < 760
    readonly property real learningHeight: Math.max(190, (height - 2 * ui.gutter) * 0.4)
    readonly property real ledgerHeight: Math.max(0, height - 2 * ui.gutter - learningHeight - 18)
    readonly property bool compactHeight: height < 620
    readonly property var sessionRecords: app.reviewSessions
    readonly property var recentRecords: sessionRecords.slice(0, 3)
    readonly property var filteredHistoryRecords: sessionRecords.filter(record => historyPage.recordDate(record) >= historyPage.historyStartDate && historyPage.recordDate(record) <= historyPage.historyEndDate)
    property string selectedSessionId: ""
    property string historyStartDate: ""
    property string historyEndDate: ""
    property bool historyCurrentWeek: true
    property Settings preferences: Settings {
        category: "historyPage"
        property bool detailsOpen: false
        property real paneWidth: 380
    }
    readonly property bool paneOpen: Boolean(selectedSessionId) && preferences.detailsOpen
    function selectedSession() {
        return sessionRecords.find(session => String(session.id || "") === selectedSessionId) || ({});
    }
    function selectSession(id) {
        selectedSessionId = id;
        preferences.detailsOpen = Boolean(id);
    }
    function startReview() {
        if (app.decks.length)
            ui.runAction("startReview");
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
    function localToday() {
        const date = new Date();
        return String(date.getFullYear()).padStart(4, "0") + "-" + String(date.getMonth() + 1).padStart(2, "0") + "-" + String(date.getDate()).padStart(2, "0");
    }
    function mondayFor(dateKey) {
        const day = parseDay(dateKey);
        if (!Number.isFinite(day))
            return dateKey;
        const offset = (new Date(day).getUTCDay() + 6) % 7;
        return dayKey(day - offset * 86400000);
    }
    function addDays(dateKey, amount) {
        const day = parseDay(dateKey);
        return Number.isFinite(day) ? dayKey(day + amount * 86400000) : dateKey;
    }
    function recordDate(record) {
        const value = record.lastReviewedAt || record.reviewedAt;
        const timestamp = new Date(value);
        if (!Number.isFinite(timestamp.getTime()))
            return "";
        return String(timestamp.getFullYear()).padStart(4, "0") + "-" + String(timestamp.getMonth() + 1).padStart(2, "0") + "-" + String(timestamp.getDate()).padStart(2, "0");
    }
    function setCurrentWeek() {
        const start = mondayFor(localToday());
        historyStartDate = start;
        historyEndDate = addDays(start, 6);
        historyCurrentWeek = true;
    }
    function shiftHistoryWeek(amount) {
        const nextStart = addDays(historyStartDate, amount * 7);
        const nextEnd = addDays(historyEndDate, amount * 7);
        if (!Number.isFinite(parseDay(nextStart)) || !Number.isFinite(parseDay(nextEnd)))
            return;
        historyStartDate = nextStart;
        historyEndDate = nextEnd;
        historyCurrentWeek = false;
    }
    function applyHistoryRange(startDate, endDate) {
        historyStartDate = startDate;
        historyEndDate = endDate;
        historyCurrentWeek = startDate === mondayFor(localToday()) && endDate === addDays(mondayFor(localToday()), 6);
    }
    Component.onCompleted: setCurrentWeek()
    onVisibleChanged: if (visible && historyCurrentWeek) setCurrentWeek()
    Timer {
        interval: 60000
        repeat: true
        running: historyPage.visible && historyPage.historyCurrentWeek
        onTriggered: historyPage.setCurrentWeek()
    }
    onSessionRecordsChanged: if (selectedSessionId && !sessionRecords.some(session => String(session.id || "") === selectedSessionId))
        selectedSessionId = ""

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: historyPage.ui.gutter
        spacing: 18

        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            clip: true
            columns: historyPage.paneOpen ? (historyPage.narrow ? 1 : 3) : (historyPage.selectedSessionId ? 2 : 1)
            columnSpacing: 0
            rowSpacing: 0
            ColumnLayout {
                id: ledger
                visible: !historyPage.narrow || !historyPage.paneOpen
                Layout.row: 0
                Layout.column: 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 0
                spacing: historyPage.compactHeight ? 8 : 12
                GridLayout {
                    columns: ledger.width < 420 && historyPage.sessionRecords.length === 0 ? 1 : 2
                    Layout.fillWidth: true
                    Layout.preferredHeight: historyPage.compactHeight ? 34 : 50
                    Label {
                        textFormat: Text.PlainText
                        text: "Recent reviews"
                        color: Theme.ink
                        font.family: Theme.contentFont
                        font.pixelSize: ledger.width < 300 ? 20 : ledger.width < 480 ? 24 : 28
                        font.weight: Font.DemiBold
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        elide: Text.ElideRight
                    }
                    AppButton {
                        visible: historyPage.sessionRecords.length === 0
                        text: "Start review"
                        primary: true
                        enabled: app.decks.length > 0 && !app.busy
                        onClicked: historyPage.startReview()
                    }
                }
                HistoryList {
                    objectName: "recentReviews"
                    ui: historyPage.ui
                    records: historyPage.recentRecords
                    selectedId: historyPage.selectedSessionId
                    rowHeight: compact ? 88 : 64
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(records.length * rowHeight, Math.max(64, historyPage.ledgerHeight * (historyPage.compactHeight ? 0.3 : 0.44)))
                    Layout.minimumHeight: 0
                    Layout.maximumHeight: Layout.preferredHeight
                    visible: records.length > 0
                    onSessionActivated: id => historyPage.selectSession(id)
                }
                Label {
                    visible: historyPage.sessionRecords.length === 0
                    textFormat: Text.PlainText
                    text: app.busy ? "Loading review history..." : "Completed reviews will appear here. Start a review to create the first entry."
                    color: Theme.inkMuted
                    font.family: Theme.contentFont
                    font.pixelSize: 16
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                RowLayout {
                    visible: historyPage.sessionRecords.length > 0
                    Layout.fillWidth: true
                    Layout.topMargin: historyPage.compactHeight ? 16 : 26
                    Label {
                        textFormat: Text.PlainText
                        text: "Review history"
                        color: Theme.ink
                        font.family: Theme.contentFont
                        font.pixelSize: ledger.width < 300 ? 20 : ledger.width < 480 ? 24 : 28
                        font.weight: Font.DemiBold
                        Layout.fillWidth: ledger.width < 700
                        Layout.minimumWidth: 0
                        elide: Text.ElideRight
                    }
                    HistoryDateControls {
                        visible: ledger.width >= 700
                        startDate: historyPage.historyStartDate
                        endDate: historyPage.historyEndDate
                        currentWeek: historyPage.historyCurrentWeek
                        Layout.preferredWidth: Math.min(430, Math.max(300, ledger.width * 0.42))
                        Layout.minimumWidth: 260
                        onPreviousRequested: historyPage.shiftHistoryWeek(-1)
                        onNextRequested: historyPage.shiftHistoryWeek(1)
                        onThisWeekRequested: historyPage.setCurrentWeek()
                        onRangeSelected: (start, end) => historyPage.applyHistoryRange(start, end)
                    }
                    Item {
                        visible: ledger.width >= 700
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                    }
                    Label {
                        visible: ledger.width >= 380
                        textFormat: Text.PlainText
                        text: historyPage.filteredHistoryRecords.length + " sessions"
                        color: Theme.inkMuted
                        font.family: Theme.monoFont
                        font.pixelSize: 12
                    }
                }
                RowLayout {
                    visible: historyPage.sessionRecords.length > 0 && ledger.width < 700
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    HistoryDateControls {
                        startDate: historyPage.historyStartDate
                        endDate: historyPage.historyEndDate
                        currentWeek: historyPage.historyCurrentWeek
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        onPreviousRequested: historyPage.shiftHistoryWeek(-1)
                        onNextRequested: historyPage.shiftHistoryWeek(1)
                        onThisWeekRequested: historyPage.setCurrentWeek()
                        onRangeSelected: (start, end) => historyPage.applyHistoryRange(start, end)
                    }
                }
                HistoryList {
                    objectName: "reviewHistory"
                    ui: historyPage.ui
                    records: historyPage.filteredHistoryRecords
                    selectedId: historyPage.selectedSessionId
                    rowHeight: compact ? 88 : 76
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 0
                    visible: historyPage.sessionRecords.length > 0 && records.length > 0
                    onSessionActivated: id => historyPage.selectSession(id)
                }
                Item {
                    visible: historyPage.sessionRecords.length > 0 && historyPage.filteredHistoryRecords.length === 0
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Label {
                        anchors.centerIn: parent
                        text: "No reviews in the selected period."
                        color: Theme.inkMuted
                        font.family: Theme.uiFont
                        font.pixelSize: 16
                    }
                }
                Item {
                    visible: historyPage.sessionRecords.length === 0
                    Layout.fillHeight: true
                }
            }
            Item {
                id: resizeHandle
                visible: !historyPage.narrow && historyPage.paneOpen
                Layout.row: 0
                Layout.column: 1
                Layout.fillHeight: true
                Layout.preferredWidth: 10
                activeFocusOnTab: true
                Accessible.name: "Resize review details. Use Left or Right; Home restores the default width."
                Keys.onLeftPressed: historyPage.preferences.paneWidth = Math.min(historyPage.width * 0.48, details.width + 24)
                Keys.onRightPressed: historyPage.preferences.paneWidth = Math.max(300, details.width - 24)
                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Home) {
                        historyPage.preferences.paneWidth = 380;
                        event.accepted = true;
                    }
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: 2
                    height: 44
                    color: resizeHandle.activeFocus ? Theme.accent : Theme.rule
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.SizeHorCursor
                    property real pressX: 0
                    property real pressWidth: 0
                    onPressed: mouse => {
                        resizeHandle.forceActiveFocus();
                        pressX = mapToItem(historyPage, mouse.x, mouse.y).x;
                        pressWidth = details.width;
                    }
                    onPositionChanged: mouse => {
                        if (pressed)
                            historyPage.preferences.paneWidth = Math.max(300, Math.min(historyPage.width * 0.48, pressWidth - (mapToItem(historyPage, mouse.x, mouse.y).x - pressX)));
                    }
                }
            }
            ReviewSessionDetails {
                id: details
                ui: historyPage.ui
                session: historyPage.selectedSession()
                visible: historyPage.paneOpen
                Layout.column: historyPage.narrow ? 0 : 2
                Layout.row: 0
                Layout.fillHeight: true
                Layout.fillWidth: historyPage.narrow
                Layout.preferredWidth: historyPage.narrow ? historyPage.width : Math.max(300, Math.min(historyPage.preferences.paneWidth, historyPage.width * 0.48))
                onCollapseRequested: historyPage.preferences.detailsOpen = false
            }
            Item {
                visible: Boolean(historyPage.selectedSessionId) && !historyPage.paneOpen
                Layout.row: 0
                Layout.column: 1
                Layout.fillHeight: true
                Layout.preferredWidth: 52
                Rectangle {
                    anchors.fill: parent
                    color: Theme.surface
                    border.color: Theme.rule
                }
                PaneToggleButton {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 2
                    collapsed: true
                    rightSide: true
                    hint: "Expand review details"
                    onClicked: historyPage.preferences.detailsOpen = true
                }
            }
        }
        LearningOverview {
            ui: historyPage.ui
            active: historyPage.visible
            Layout.fillWidth: true
            Layout.preferredHeight: historyPage.learningHeight
            Layout.minimumHeight: historyPage.learningHeight
            Layout.maximumHeight: historyPage.learningHeight
        }
    }
}
