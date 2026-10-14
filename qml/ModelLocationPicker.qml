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

    columns: width >= locationLabel.implicitWidth + locationChoices.implicitWidth + columnSpacing ? 3 : 1
    columnSpacing: 16
    rowSpacing: 8
    Layout.fillWidth: true
    Layout.minimumWidth: 0
    implicitWidth: locationLabel.implicitWidth + locationChoices.implicitWidth + columnSpacing
    implicitHeight: columns > 1 ? Math.max(locationLabel.implicitHeight, locationChoices.implicitHeight) : locationLabel.implicitHeight + locationChoices.implicitHeight + rowSpacing

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
        text: "Location of the model:"
        color: Theme.ink
        font.pixelSize: 17
        font.weight: Font.DemiBold
        Layout.fillWidth: root.columns === 1
        Layout.preferredWidth: implicitWidth
        Layout.maximumWidth: implicitWidth
        Layout.minimumWidth: 0
        elide: Text.ElideRight
        ToolTip.visible: truncated && hovered
        ToolTip.text: text
    }

    Item {
        id: locationChoices
        implicitWidth: 240
        implicitHeight: 36
        Layout.preferredWidth: implicitWidth
        Layout.minimumWidth: 0
        Layout.maximumWidth: implicitWidth
        Layout.alignment: Qt.AlignLeft

        Canvas {
            anchors.fill: parent
            antialiasing: true
            readonly property var paintPalette: [
                root.currentIndex === 0 ? Theme.modelLocationLocalFill : Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.18),
                root.currentIndex === 1 ? Theme.info : Qt.rgba(Theme.info.r, Theme.info.g, Theme.info.b, 0.18),
                root.currentIndex === 0 || localButton.activeFocus ? Theme.warning : Theme.rule,
                root.currentIndex === 1 || remoteButton.activeFocus ? Theme.info : Theme.rule,
                localButton.activeFocus || root.currentIndex === 0 ? 2 : 1,
                remoteButton.activeFocus || root.currentIndex === 1 ? 2 : 1,
                Theme.inkMuted
            ]
            onPaintPaletteChanged: requestPaint()
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                const context = getContext("2d")
                context.reset()
                const inset = 1
                const right = width - inset
                const bottom = height - inset
                const middle = width / 2
                const slant = 7
                const radius = 6
                context.lineJoin = "round"

                context.beginPath()
                context.moveTo(inset + radius, inset)
                context.lineTo(middle + slant, inset)
                context.lineTo(middle - slant, bottom)
                context.lineTo(inset + radius, bottom)
                context.quadraticCurveTo(inset, bottom, inset, bottom - radius)
                context.lineTo(inset, inset + radius)
                context.quadraticCurveTo(inset, inset, inset + radius, inset)
                context.closePath()
                context.fillStyle = paintPalette[0]
                context.fill()
                context.strokeStyle = paintPalette[2]
                context.lineWidth = paintPalette[4]
                context.stroke()

                context.beginPath()
                context.moveTo(middle + slant, inset)
                context.lineTo(right - radius, inset)
                context.quadraticCurveTo(right, inset, right, inset + radius)
                context.lineTo(right, bottom - radius)
                context.quadraticCurveTo(right, bottom, right - radius, bottom)
                context.lineTo(middle - slant, bottom)
                context.closePath()
                context.fillStyle = paintPalette[1]
                context.fill()
                context.strokeStyle = paintPalette[3]
                context.lineWidth = paintPalette[5]
                context.stroke()

                context.beginPath()
                context.moveTo(middle + slant, inset)
                context.lineTo(middle - slant, bottom)
                context.strokeStyle = paintPalette[6]
                context.lineWidth = 1.5
                context.stroke()
            }
        }

        RowLayout {
            anchors.fill: parent
            spacing: 0

            Button {
                id: localButton
                Layout.fillWidth: true
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
                background: Item {}
                onClicked: root.select(0)
                Keys.onPressed: event => root.handleNavigation(event)
            }

            Button {
                id: remoteButton
                Layout.fillWidth: true
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
                background: Item {}
                onClicked: root.select(1)
                Keys.onPressed: event => root.handleNavigation(event)
            }
        }
    }

    Item { visible: root.columns > 1; Layout.fillWidth: true }
}
