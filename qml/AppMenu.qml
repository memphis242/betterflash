import QtQuick
import QtQuick.Controls

Menu {
    id: menu

    delegate: AppMenuItem {}
    background: Item {
        implicitWidth: 340
        implicitHeight: 8
        Rectangle {
            anchors.fill: parent
            anchors.topMargin: 3
            anchors.leftMargin: 2
            anchors.rightMargin: -2
            anchors.bottomMargin: -3
            color: Theme.scrim
            opacity: 0.5
            radius: 13
        }
        Rectangle {
            anchors.fill: parent
            color: Theme.surfaceRaised
            radius: 12
            border.width: 1
            border.color: Theme.rule
        }
    }
    topPadding: 8
    bottomPadding: 8
    leftPadding: 8
    rightPadding: 8
}
