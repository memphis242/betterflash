import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ListView {
    id: history
    required property var ui
    property var records: []
    property string selectedId: ""
    property real rowHeight: 76
    readonly property bool compact: width < 600
    signal sessionActivated(string sessionId)
    model: records
    clip: true
    spacing: 0
    ScrollBar.vertical: ScrollBar {}
    delegate: ItemDelegate {
        id: row
        required property var modelData
        width: ListView.view.width
        height: history.rowHeight
        highlighted: String(modelData.id || "") === history.selectedId
        onClicked: history.sessionActivated(String(modelData.id || ""))
        Accessible.name: String(modelData.deckName || "Review") + ", " + history.ui.dateTime(modelData.lastReviewedAt || modelData.reviewedAt)
        ToolTip.visible: hovered && Boolean(modelData.estimated)
        ToolTip.text: "This session was inferred from nearby review records."
        contentItem: ColumnLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            anchors.topMargin: 10
            anchors.bottomMargin: 10
            spacing: 5
            RowLayout {
                visible: !history.compact
                Layout.fillWidth: true
                Label {
                    textFormat: Text.PlainText
                    text: String(row.modelData.deckName || "Review")
                    color: Theme.ink
                    font.family: Theme.contentFont
                    font.pixelSize: 17
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    elide: Text.ElideRight
                }
                Label {
                    textFormat: Text.PlainText
                    text: history.ui.dateTime(row.modelData.lastReviewedAt || row.modelData.reviewedAt)
                    color: Theme.inkMuted
                    font.family: Theme.monoFont
                    font.pixelSize: 11
                    Layout.minimumWidth: 0
                }
            }
            ColumnLayout {
                visible: history.compact
                Layout.fillWidth: true
                spacing: 2
                Label {
                    textFormat: Text.PlainText
                    text: String(row.modelData.deckName || "Review")
                    color: Theme.ink
                    font.family: Theme.contentFont
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    elide: Text.ElideRight
                }
                Label {
                    textFormat: Text.PlainText
                    text: history.ui.dateTime(row.modelData.lastReviewedAt || row.modelData.reviewedAt)
                    color: Theme.inkMuted
                    font.family: Theme.monoFont
                    font.pixelSize: 10
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    elide: Text.ElideRight
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 16
                Label {
                    textFormat: Text.PlainText
                    text: Number(row.modelData.cardCount || 0) + " cards · " + Number(row.modelData.reviewCount || 0) + " answers"
                    color: Theme.inkMuted
                    font.family: Theme.monoFont
                    font.pixelSize: 11
                    Layout.minimumWidth: 0
                    elide: Text.ElideRight
                    Layout.fillWidth: history.compact
                }
                Label {
                    textFormat: Text.PlainText
                    text: "Recall " + Math.round(Number(row.modelData.averageRecall || 0) * 100) + "%"
                    color: history.recallColor(row.modelData)
                    font.family: Theme.monoFont
                    font.pixelSize: 11
                }
                Label {
                    visible: Boolean(row.modelData.estimated) && !history.compact
                    textFormat: Text.PlainText
                    text: "Estimated session"
                    color: Theme.inkMuted
                    font.family: Theme.monoFont
                    font.pixelSize: 11
                    ToolTip.visible: inferredHover.hovered
                    ToolTip.text: "This session was inferred from nearby review records."
                    HoverHandler {
                        id: inferredHover
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
            }
        }
        background: Rectangle {
            color: row.highlighted ? Theme.accentSoft : (row.hovered ? Theme.ledgerSurfaceRaised : Theme.transparent)
            border.width: row.highlighted ? 2 : 0
            border.color: Theme.accent
            Rectangle {
                width: parent.width
                height: 1
                anchors.bottom: parent.bottom
                color: Theme.ledgerRule
            }
        }
    }
    function recallColor(session) {
        const grade = Math.max(0, Math.min(4, Math.round(Number(session.averageGrade || 0))));
        return [Theme.recallMissed, Theme.recallPartial, Theme.recallHard, Theme.recallGood, Theme.recallEasy][grade];
    }
}
