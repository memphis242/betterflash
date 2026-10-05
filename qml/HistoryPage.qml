import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: historyPage
    required property var ui
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: historyPage.ui.gutter
        spacing: 16
        RowLayout {
            objectName: "historyTopBar"
            Layout.fillWidth: true
            Label {
                textFormat: Text.PlainText
                text: "Recent reviews"
                color: Theme.ink
                font.family: Theme.contentFont
                font.pixelSize: historyPage.ui.width < 600 ? 24 : 28
                Layout.fillWidth: true
            }
            AppButton {
                objectName: "historyPrimary"
                text: app.history.length ? "Review history" : "Start review"
                primary: true
                enabled: app.history.length > 0 || app.decks.length > 0
                onClicked: {
                    if (app.history.length)
                        historyPage.ui.openAllHistory()
                    else
                        historyPage.ui.runAction("startReview")
                }
            }
        }
        HistoryList {
            objectName: "historyScroll"
            visible: app.history.length > 0
            ui: historyPage.ui
            records: app.history.slice(0, 6)
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
        Label {
            textFormat: Text.PlainText
            visible: app.history.length === 0
            text: "Completed reviews will appear here."
            color: Theme.inkMuted
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            Layout.alignment: Qt.AlignTop
        }
        Item {
            visible: app.history.length === 0
            Layout.fillHeight: true
        }
    }
}
