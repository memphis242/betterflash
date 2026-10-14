import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

GridLayout {
    id: root

    // The containing settings page owns the provider value. The control emits
    // the user's choice so provider changes remain explicit and reversible.
    property int currentIndex: 0
    property string localHint: "Runs on this machine and works offline after the model is installed."
    property string remoteHint: "Uses a configured cloud provider and requires a network connection."
    signal activated(int index)

    columns: width >= 520 ? 2 : 1
    columnSpacing: 16
    rowSpacing: 8
    Layout.fillWidth: true
    Layout.minimumWidth: 0
    implicitHeight: columns === 2 ? Math.max(locationLabel.implicitHeight, locationChoices.implicitHeight) : locationLabel.implicitHeight + locationChoices.implicitHeight + rowSpacing

    function select(index) {
        const selectedIndex = index === 0 ? 0 : 1
        if (selectedIndex !== root.currentIndex)
            root.activated(selectedIndex)
        if (selectedIndex === 0)
            localButton.forceActiveFocus()
        else
            remoteButton.forceActiveFocus()
    }

    function handleNavigation(event) {
        switch (event.key) {
        case Qt.Key_Left:
        case Qt.Key_Home:
            root.select(0)
            event.accepted = true
            break
        case Qt.Key_Right:
        case Qt.Key_End:
            root.select(1)
            event.accepted = true
            break
        }
    }

    Label {
        id: locationLabel
        text: "Location of model:"
        color: Theme.ink
        font.pixelSize: 17
        font.weight: Font.DemiBold
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        elide: Text.ElideRight
        ToolTip.visible: truncated && hovered
        ToolTip.text: text
    }

    RowLayout {
        id: locationChoices
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.maximumWidth: 240
        Layout.alignment: Qt.AlignRight
        spacing: 0

        Button {
            id: localButton
            Layout.preferredWidth: 120
            Layout.minimumWidth: 0
            Layout.preferredHeight: 36
            text: "Local"
            font.family: Theme.uiFont
            font.pixelSize: 15
            font.weight: root.currentIndex === 0 ? Font.DemiBold : Font.Normal
            checkable: false
            Accessible.name: "Local (on your machine)"
            Accessible.description: root.localHint
            Accessible.role: Accessible.RadioButton
            Accessible.checkable: true
            Accessible.checked: root.currentIndex === 0
            Accessible.onPressAction: root.select(0)
            ToolTip.delay: 350
            ToolTip.visible: hovered
            ToolTip.text: "Local (on your machine): " + root.localHint
            contentItem: Label {
                text: localButton.text
                color: root.currentIndex === 0 ? Theme.modelLocationLocalInk : Theme.modelLocationLocalMutedInk
                font: localButton.font
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: root.currentIndex === 0 ? Theme.modelLocationLocalFill : Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.18)
                border.color: root.currentIndex === 0 || localButton.activeFocus ? Theme.warning : Theme.rule
                border.width: localButton.activeFocus || root.currentIndex === 0 ? 2 : 1
                radius: 6
            }
            onClicked: root.select(0)
            Keys.onPressed: event => root.handleNavigation(event)
        }

        Canvas {
            property color separatorColor: Theme.rule
            Layout.preferredWidth: 8
            Layout.preferredHeight: 36
            z: 2
            onSeparatorColorChanged: requestPaint()
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                ctx.strokeStyle = separatorColor
                ctx.lineWidth = 1
                ctx.beginPath()
                ctx.moveTo(1, 0)
                ctx.lineTo(width - 1, height)
                ctx.stroke()
            }
        }

        Button {
            id: remoteButton
            Layout.preferredWidth: 120
            Layout.minimumWidth: 0
            Layout.preferredHeight: 36
            text: "Remote"
            font.family: Theme.uiFont
            font.pixelSize: 15
            font.weight: root.currentIndex === 1 ? Font.DemiBold : Font.Normal
            checkable: false
            Accessible.name: "Remote (cloud-provider)"
            Accessible.description: root.remoteHint
            Accessible.role: Accessible.RadioButton
            Accessible.checkable: true
            Accessible.checked: root.currentIndex === 1
            Accessible.onPressAction: root.select(1)
            ToolTip.delay: 350
            ToolTip.visible: hovered
            ToolTip.text: "Remote (cloud-provider): " + root.remoteHint
            contentItem: Label {
                text: remoteButton.text
                color: root.currentIndex === 1 ? Theme.onAccent : Theme.info
                font: remoteButton.font
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: root.currentIndex === 1 ? Theme.info : Qt.rgba(Theme.info.r, Theme.info.g, Theme.info.b, 0.18)
                border.color: root.currentIndex === 1 || remoteButton.activeFocus ? Theme.info : Theme.rule
                border.width: remoteButton.activeFocus || root.currentIndex === 1 ? 2 : 1
                radius: 6
            }
            onClicked: root.select(1)
            Keys.onPressed: event => root.handleNavigation(event)
        }
    }
}
