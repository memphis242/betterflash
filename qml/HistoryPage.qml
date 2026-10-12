pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore

Item {
    id: historyPage
    required property var ui
    readonly property bool narrow: width < 760
    readonly property var sessionRecords: app.reviewSessions
    readonly property var recentRecords: sessionRecords.slice(0, 3)
    property string selectedSessionId: ""
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
    onSessionRecordsChanged: if (selectedSessionId && !sessionRecords.some(session => String(session.id || "") === selectedSessionId))
        selectedSessionId = ""

    GridLayout {
        anchors.fill: parent
        anchors.margins: historyPage.ui.gutter
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
            spacing: 12
            GridLayout {
                columns: ledger.width < 420 && historyPage.sessionRecords.length === 0 ? 1 : 2
                Layout.fillWidth: true
                Layout.preferredHeight: 50
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
                Layout.preferredHeight: Math.min(records.length * rowHeight, ledger.height * 0.45)
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
                Layout.topMargin: 8
                Label {
                    textFormat: Text.PlainText
                    text: "Review history"
                    color: Theme.ink
                    font.family: Theme.contentFont
                    font.pixelSize: ledger.width < 300 ? 20 : ledger.width < 480 ? 24 : 28
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    elide: Text.ElideRight
                }
                Label {
                    visible: ledger.width >= 380
                    textFormat: Text.PlainText
                    text: historyPage.sessionRecords.length + " sessions"
                    color: Theme.inkMuted
                    font.family: Theme.monoFont
                    font.pixelSize: 12
                }
            }
            HistoryList {
                objectName: "reviewHistory"
                ui: historyPage.ui
                records: historyPage.sessionRecords
                selectedId: historyPage.selectedSessionId
                rowHeight: compact ? 88 : 76
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: records.length > 0
                onSessionActivated: id => historyPage.selectSession(id)
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
}
