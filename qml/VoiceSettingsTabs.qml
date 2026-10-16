import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property int currentIndex: 0
    property int styleIndex: 0
    signal tabSelected(int index)
    implicitHeight: styleIndex === 2 ? 68 : styleIndex === 1 ? 54 : 52
    Layout.fillWidth: true
    activeFocusOnTab: true
    Keys.onLeftPressed: root.select(0)
    Keys.onRightPressed: root.select(1)
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Home || event.key === Qt.Key_End) {
            root.select(event.key === Qt.Key_Home ? 0 : 1)
            event.accepted = true
        }
    }

    function select(index) {
        const next = Math.max(0, Math.min(1, index))
        root.currentIndex = next
        root.tabSelected(next)
    }

    Rectangle {
        visible: root.styleIndex === 0
        anchors.fill: parent
        color: Theme.surfaceRaised
        border.color: Theme.rule
        border.width: 1
        radius: 7
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: root.styleIndex === 0 ? 4 : 0
        spacing: root.styleIndex === 2 ? 10 : 6

        Repeater {
            model: [
                { title: "Speech recognition", shortTitle: "Recognition", icon: "ear", hint: "Configure speech recognition" },
                { title: "Speech output", shortTitle: "Output", icon: "mouth-sound", hint: "Configure speech output" }
            ]
            delegate: Button {
                id: tab
                required property var modelData
                required property int index
                Layout.fillWidth: true
                Layout.fillHeight: true
                implicitHeight: root.implicitHeight
                flat: true
                checkable: true
                checked: root.currentIndex === index
                activeFocusOnTab: true
                text: modelData.title
                Accessible.name: modelData.title
                Accessible.description: modelData.hint
                onClicked: root.select(index)
                Keys.onLeftPressed: root.select(index - 1)
                Keys.onRightPressed: root.select(index + 1)
                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Home || event.key === Qt.Key_End) {
                        root.select(event.key === Qt.Key_Home ? 0 : 1)
                        event.accepted = true
                    }
                }
                contentItem: RowLayout {
                    spacing: root.styleIndex === 2 ? 10 : 8
                    Icon {
                        kind: modelData.icon
                        Layout.preferredWidth: root.styleIndex === 2 ? 27 : 23
                        Layout.preferredHeight: Layout.preferredWidth
                        stroke: tab.checked && root.styleIndex === 1 ? Theme.onAccent : tab.checked ? Theme.accent : Theme.inkMuted
                    }
                    Label {
                        Layout.fillWidth: true
                        text: root.styleIndex === 1 ? modelData.shortTitle : modelData.title
                        color: tab.checked && root.styleIndex === 1 ? Theme.onAccent : tab.checked ? Theme.ink : Theme.inkMuted
                        font.family: Theme.uiFont
                        font.pixelSize: root.styleIndex === 2 ? 16 : 15
                        font.bold: tab.checked
                        elide: Text.ElideRight
                    }
                }

                background: Rectangle {
                    readonly property bool selected: root.currentIndex === index
                    radius: root.styleIndex === 0 ? 5 : root.styleIndex === 1 ? 20 : 8
                    color: root.styleIndex === 1
                        ? (selected ? Theme.accent : Theme.surfaceRaised)
                        : selected ? Theme.accentSoft : Theme.transparent
                    border.color: tab.activeFocus
                        ? Theme.accent
                        : root.styleIndex === 2
                        ? (selected ? Theme.accent : Theme.rule)
                        : root.styleIndex === 1 ? Theme.rule : selected ? Theme.accent : Theme.rule
                    border.width: tab.activeFocus || root.styleIndex === 2 || root.styleIndex === 1 || selected ? 1 : 0
                    Rectangle {
                        visible: root.styleIndex === 0
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: parent.selected ? 3 : 1
                        color: parent.selected ? Theme.accent : Theme.rule
                    }
                }
            }
        }
    }
}
