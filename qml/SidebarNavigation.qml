import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: rail
    property bool collapsed: false
    property bool distributed: false
    property bool toggleVisible: true
    property int currentIndex: 0
    property int toggleVariant: 0
    property string reviewGlyph: "review-flip"
    property var bindings: ({})
    readonly property var expandGlyphs: ["menu-bars", "chevron-right-bold", "panel-open", "double-right", "rail-unfold"]
    readonly property var collapseGlyphs: ["dock-left", "chevron-left-bold", "panel-close", "double-left", "rail-fold"]
    signal toggleRequested()
    signal pageRequested(int index)
    implicitWidth: collapsed ? 76 : 218
    color: Theme.surface
    clip: true

    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.rule }

    Item {
        id: toggleArea
        width: parent.width
        height: rail.toggleVisible ? 64 : 12
        visible: rail.toggleVisible
        ToolButton {
            id: toggle
            objectName: "navToggle"
            anchors.centerIn: parent
            width: rail.collapsed ? 52 : parent.width - 20
            height: 48
            padding: 0
            ToolTip.visible: hovered || activeFocus
            ToolTip.text: rail.collapsed ? "Expand navigation" : "Collapse navigation"
            Accessible.name: ToolTip.text
            onClicked: rail.toggleRequested()
            contentItem: Item {
                Icon {
                    anchors.centerIn: parent
                    width: 26
                    height: 26
                    kind: rail.collapsed ? rail.expandGlyphs[rail.toggleVariant] : rail.collapseGlyphs[rail.toggleVariant]
                    stroke: toggle.hovered || toggle.activeFocus ? Theme.ink : Theme.inkMuted
                }
            }
            background: Rectangle {
                radius: 7
                color: toggle.hovered || toggle.down ? Theme.accentSoft : Theme.transparent
                border.width: toggle.activeFocus ? 2 : 0
                border.color: Theme.accent
            }
        }
    }

    ColumnLayout {
        anchors.top: toggleArea.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: rail.distributed ? 0 : 10
        anchors.rightMargin: rail.distributed ? 1 : 10
        anchors.bottomMargin: rail.distributed ? 0 : 10
        spacing: rail.distributed ? 0 : 8
        Repeater {
            model: [
                {name: "Review", glyph: rail.reviewGlyph, action: "reviewPage"},
                {name: "Library", glyph: "bookshelf", action: "libraryPage"},
                {name: "History", glyph: "clock", action: "historyPage"},
                {name: "Settings", glyph: "gear-toothed", action: "settingsPage"}
            ]
            delegate: Button {
                id: navButton
                required property var modelData
                required property int index
                readonly property bool selected: rail.currentIndex === index
                objectName: "nav" + modelData.name
                Layout.fillWidth: true
                Layout.fillHeight: rail.distributed
                Layout.preferredHeight: 56
                Layout.minimumHeight: 52
                padding: 0
                ToolTip.visible: hovered || activeFocus
                ToolTip.text: modelData.name + (rail.bindings[modelData.action] ? " (" + rail.bindings[modelData.action] + ")" : "")
                Accessible.name: ToolTip.text
                onClicked: rail.pageRequested(index)
                contentItem: Item {
                    Icon {
                        id: navIcon
                        x: rail.collapsed ? (parent.width - width) / 2 : 16
                        anchors.verticalCenter: parent.verticalCenter
                        width: 26
                        height: 26
                        kind: navButton.modelData.glyph
                        stroke: navButton.selected ? Theme.accent : Theme.inkMuted
                        lineWidth: 1.35
                    }
                    Label {
                        anchors.left: navIcon.right
                        anchors.leftMargin: 14
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !rail.collapsed
                        text: navButton.modelData.name
                        color: navButton.selected ? Theme.accent : Theme.inkMuted
                        font.pixelSize: 15
                        elide: Text.ElideRight
                    }
                }
                background: Rectangle {
                    radius: rail.distributed ? 0 : 7
                    color: navButton.hovered || navButton.down ? Theme.accentSoft : Theme.transparent
                    border.width: (navButton.selected && !rail.distributed) || navButton.activeFocus ? 2 : 0
                    border.color: Theme.accent
                    Rectangle {
                        visible: rail.distributed
                        width: parent.width
                        height: 1
                        color: Theme.rule
                    }
                    Rectangle {
                        visible: rail.distributed && navButton.selected
                        width: 3
                        height: parent.height
                        color: Theme.accent
                    }
                }
            }
        }
        Item { visible: !rail.distributed; Layout.fillHeight: true }
    }
}
