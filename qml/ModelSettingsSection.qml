import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Frame {
    id: root

    property string title: ""
    property bool showTitle: title.length > 0

    Layout.fillWidth: true
    Layout.minimumWidth: 0
    Layout.bottomMargin: 36
    padding: 20
    implicitHeight: implicitContentHeight + topPadding + bottomPadding
    background: Rectangle {
        color: Theme.surfaceRaised
        border.color: Theme.rule
        border.width: 1
        radius: 10
    }
    contentItem: ColumnLayout {
        id: contentColumn
        spacing: 14

        Label {
            visible: root.showTitle
            text: root.title
            color: Theme.ink
            font.pixelSize: 18
            font.weight: Font.DemiBold
            Layout.fillWidth: true
        }
    }
}
