import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    // The containing settings page owns the provider value. The control emits
    // the user's choice so provider changes remain explicit and reversible.
    property int currentIndex: 0
    property string localHint: "Runs on this machine and works offline after the model is installed."
    property string remoteHint: "Uses a configured cloud provider and requires a network connection."
    signal activated(int index)
    spacing: 10
    Layout.fillWidth: true
    Layout.minimumWidth: 0
    Layout.bottomMargin: 18
    implicitHeight: locationFrame.implicitHeight + 10

    function select(index) {
        const selectedIndex = index === 0 ? 0 : 1
        root.activated(selectedIndex)
        if (selectedIndex === 0)
            localButton.forceActiveFocus()
        else
            remoteButton.forceActiveFocus()
    }

    Rectangle {
        id: locationFrame
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        implicitHeight: locationLayout.implicitHeight + 28
        color: Theme.surfaceRaised
        border.color: Theme.rule
        border.width: 1
        radius: 8

        ColumnLayout {
            id: locationLayout
            anchors.fill: parent
            anchors.margins: 14
            spacing: 10

            Label {
                text: "Location of model:"
                color: Theme.ink
                font.pixelSize: 17
                font.weight: Font.DemiBold
                Layout.fillWidth: true
                elide: Text.ElideRight
                ToolTip.visible: truncated && hovered
                ToolTip.text: text
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 0

                Button {
                    id: localButton
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    implicitHeight: Math.max(46, implicitContentHeight + 16)
                    text: "Local (on your machine)"
                    font.family: Theme.uiFont
                    font.pixelSize: 17
                    font.weight: root.currentIndex === 0 ? Font.DemiBold : Font.Normal
                    checkable: false
                    Accessible.name: text
                    Accessible.description: root.localHint
                    Accessible.role: Accessible.RadioButton
                    Accessible.checkable: true
                    Accessible.checked: root.currentIndex === 0
                    Accessible.onPressAction: root.select(0)
                    ToolTip.delay: 350
                    ToolTip.visible: hovered
                    ToolTip.text: root.localHint
                    contentItem: Label {
                        text: localButton.text
                        color: localButton.enabled ? (root.currentIndex === 0 ? Theme.ink : Theme.inkMuted) : Theme.inkMuted
                        font: localButton.font
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        wrapMode: Text.Wrap
                        elide: Text.ElideNone
                    }
                    background: Rectangle {
                        color: root.currentIndex === 0 || localButton.hovered ? Theme.accentSoft : Theme.transparent
                        border.color: root.currentIndex === 0 || localButton.activeFocus ? Theme.accent : Theme.rule
                        border.width: localButton.activeFocus || root.currentIndex === 0 ? 2 : 1
                        radius: 6
                    }
                    onClicked: root.select(0)
                    Keys.onLeftPressed: root.select(0)
                    Keys.onRightPressed: root.select(1)
                    Keys.onHomePressed: root.select(0)
                    Keys.onEndPressed: root.select(1)
                }

                Button {
                    id: remoteButton
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    implicitHeight: Math.max(46, implicitContentHeight + 16)
                    text: "Remote (cloud-provider)"
                    font.family: Theme.uiFont
                    font.pixelSize: 17
                    font.weight: root.currentIndex === 1 ? Font.DemiBold : Font.Normal
                    checkable: false
                    Accessible.name: text
                    Accessible.description: root.remoteHint
                    Accessible.role: Accessible.RadioButton
                    Accessible.checkable: true
                    Accessible.checked: root.currentIndex === 1
                    Accessible.onPressAction: root.select(1)
                    ToolTip.delay: 350
                    ToolTip.visible: hovered
                    ToolTip.text: root.remoteHint
                    contentItem: Label {
                        text: remoteButton.text
                        color: remoteButton.enabled ? (root.currentIndex === 1 ? Theme.ink : Theme.inkMuted) : Theme.inkMuted
                        font: remoteButton.font
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        wrapMode: Text.Wrap
                        elide: Text.ElideNone
                    }
                    background: Rectangle {
                        color: root.currentIndex === 1 || remoteButton.hovered ? Theme.accentSoft : Theme.transparent
                        border.color: root.currentIndex === 1 || remoteButton.activeFocus ? Theme.accent : Theme.rule
                        border.width: remoteButton.activeFocus || root.currentIndex === 1 ? 2 : 1
                        radius: 6
                    }
                    onClicked: root.select(1)
                    Keys.onLeftPressed: root.select(0)
                    Keys.onRightPressed: root.select(1)
                    Keys.onHomePressed: root.select(0)
                    Keys.onEndPressed: root.select(1)
                }
            }
        }
    }
}
