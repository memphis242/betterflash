import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ListView {
    id: history
    required property var ui
    property var records: []
    model: records
    clip: true
    ScrollBar.vertical: ScrollBar {}
    delegate: ItemDelegate {
        required property var modelData
        width: ListView.view.width
        height: history.ui.width < 600 ? 112 : 80
        contentItem: ColumnLayout {
            spacing: 6
            RowLayout {
                Layout.fillWidth: true
                Label {
                    textFormat: Text.PlainText
                    text: modelData.deckName
                    color: Theme.ink
                    font.family: Theme.contentFont
                    font.pixelSize: 18
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
                Label {
                    textFormat: Text.PlainText
                    text: history.ui.gradeLabel(modelData.grade)
                    color: history.ui.gradeLabel(modelData.grade) === "Partial" ? Theme.warning : history.ui.gradeLabel(modelData.grade) === "Good" || history.ui.gradeLabel(modelData.grade) === "Easy" ? Theme.success : Theme.inkMuted
                    font.family: Theme.monoFont
                    font.pixelSize: 11
                }
            }
            Flow {
                Layout.fillWidth: true
                spacing: 16
                Label {
                    textFormat: Text.PlainText
                    text: history.ui.dateTime(modelData.reviewedAt)
                    color: Theme.inkMuted
                    font.family: Theme.monoFont
                    font.pixelSize: 11
                }
                Label {
                    textFormat: Text.PlainText
                    text: Number(modelData.responseSeconds).toFixed(1) + " s"
                    color: Theme.inkMuted
                    font.family: Theme.monoFont
                    font.pixelSize: 11
                }
                Label {
                    textFormat: Text.PlainText
                    text: "Recall " + Number(modelData.recallFraction).toFixed(2)
                    color: Theme.inkMuted
                    font.family: Theme.monoFont
                    font.pixelSize: 11
                }
                Label {
                    textFormat: Text.PlainText
                    text: "Next " + history.ui.dateTime(modelData.due)
                    color: Theme.inkMuted
                    font.family: Theme.monoFont
                    font.pixelSize: 11
                }
            }
        }
        background: Rectangle {
            color: parent.hovered ? Theme.accentSoft : Theme.transparent
            Rectangle {
                width: parent.width
                height: 1
                anchors.bottom: parent.bottom
                color: Theme.rule
            }
        }
    }
}
