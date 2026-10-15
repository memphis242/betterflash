import QtQuick
import QtQuick.Controls

ComboBox {
    id: control

    implicitHeight: Math.max(38, font.pixelSize + 25)
    implicitWidth: Math.max(180, contentItem.implicitWidth + indicator.width + 18)
    leftPadding: Math.max(8, Math.min(12, height / 3))
    rightPadding: indicator.width + 2
    topPadding: 0
    bottomPadding: 0
    font.family: Theme.uiFont

    palette.buttonText: control.enabled ? Theme.ink : Theme.inkMuted
    palette.text: control.enabled ? Theme.ink : Theme.inkMuted
    palette.placeholderText: Theme.inkMuted
    property string comboHint: "Choose an option"
    ToolTip.visible: hovered && comboHint.length > 0
    ToolTip.text: comboHint

    indicator: Item {
        x: control.width - width
        y: 1
        width: Math.max(24, Math.min(40, control.height - 8))
        height: control.height - 2

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 1
            color: control.enabled ? Theme.rule : Theme.inkMuted
        }
        Icon {
            anchors.centerIn: parent
            width: 17
            height: 17
            kind: "chevron"
            lineWidth: 1.8
            stroke: control.enabled ? Theme.ink : Theme.inkMuted
        }
    }

    background: Rectangle {
        radius: 5
        color: !control.enabled ? Theme.comboDisabled
                               : control.popup.visible ? Theme.comboOpen
                               : control.hovered ? Theme.comboHover
                               : Theme.comboFill
        border.width: control.activeFocus || control.popup.visible ? 2 : 1
        border.color: !control.enabled ? Theme.rule
                                       : control.activeFocus || control.popup.visible ? Theme.accent
                                       : Theme.rule
    }

    popup.background: Rectangle {
        color: Theme.surfaceRaised
        border.color: Theme.rule
        border.width: 1
        radius: 5
    }
}
