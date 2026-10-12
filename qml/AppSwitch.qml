import QtQuick
import QtQuick.Controls

Switch {
    id: control
    padding: 4
    spacing: 12
    font.family: Theme.uiFont
    font.pixelSize: 15
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    implicitWidth: Math.max(implicitIndicatorWidth + leftPadding + rightPadding, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitIndicatorHeight, implicitContentHeight) + topPadding + bottomPadding

    indicator: Rectangle {
        id: track
        implicitWidth: 48
        implicitHeight: 26
        x: control.text.length > 0 ? (control.mirrored ? control.width - width - control.rightPadding : control.leftPadding) : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: height / 2
        color: control.checked ? Theme.success : Theme.rule
        border.width: 1
        border.color: control.checked ? Theme.success : Theme.inkMuted
        opacity: control.enabled ? 1 : 0.5

        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            radius: height / 2
            color: Theme.transparent
            border.width: 2
            border.color: Theme.accent
            visible: control.activeFocus
        }
        Rectangle {
            width: 20
            height: 20
            x: 3 + control.visualPosition * (track.width - width - 6)
            anchors.verticalCenter: parent.verticalCenter
            radius: height / 2
            color: control.checked ? Theme.canvas : Theme.inkMuted
            Behavior on x {
                enabled: !control.down && !Theme.reducedMotion
                NumberAnimation {
                    duration: 120
                    easing.type: Easing.OutCubic
                }
            }
        }
    }
    contentItem: Label {
        text: control.text
        textFormat: Text.PlainText
        font: control.font
        color: control.checked && control.enabled ? Theme.ink : Theme.inkMuted
        leftPadding: control.indicator && !control.mirrored ? control.indicator.width + control.spacing : 0
        rightPadding: control.indicator && control.mirrored ? control.indicator.width + control.spacing : 0
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.Wrap
    }
}
