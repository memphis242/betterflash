pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: gallery
    objectName: "menuDesignGallery"
    width: 1280
    height: 860
    minimumWidth: 900
    minimumHeight: 860
    visible: true
    title: "BetterFlash - Menu studies"
    color: Theme.canvas
    font.family: Theme.uiFont
    font.pixelSize: 14
    palette.window: Theme.canvas
    palette.windowText: Theme.ink
    palette.text: Theme.ink
    palette.buttonText: Theme.ink
    palette.base: Theme.surface
    palette.highlight: Theme.accentSoft
    palette.highlightedText: Theme.ink

    property bool deckMode: false
    property string actionStatus: "Choose a row to try an action. These studies leave your library unchanged."
    readonly property var designs: [
        { name: "Ruled ledger", description: "Fine rules, square rhythm, compact rows." },
        { name: "Soft rows", description: "Inset rows, generous spacing, soft corners." },
        { name: "Selection rail", description: "Open rows with a narrow selection marker." }
    ]
    readonly property var cardActions: [
        { title: "View card", glyph: "cards", key: "Enter", group: 0, tip: "Open the card's front, back and review timeline." },
        { title: "Edit card", glyph: "edit", key: "Ctrl+E", group: 0, tip: "Edit the question, answer, tags and card format." },
        { title: "Move card", glyph: "arrow-right", key: "M", group: 0, tip: "Choose a destination deck for this card." },
        { title: "Atomicize card", glyph: "review-stack", key: "", group: 1, tip: "Preview focused cards split from distinct ideas." },
        { title: "Reset card", glyph: "return-card", key: "", group: 1, tip: "Reset review scheduling and statistics while keeping the card." },
        { title: "Delete card", glyph: "close", key: "Del", group: 2, danger: true, tip: "Remove the card after confirmation." }
    ]
    readonly property var deckActions: [
        { title: "Add card", glyph: "plus", key: "N", group: 0, tip: "Add a card to this deck." },
        { title: "Add subdeck", glyph: "folder", key: "", group: 0, tip: "Create a deck inside this deck." },
        { title: "Rename deck", glyph: "edit", key: "F2", group: 0, tip: "Change the deck's name." },
        { title: "Start review", glyph: "review-flip", key: "Enter", group: 1, tip: "Start reviewing the due cards in this deck." },
        { title: "Reset deck state", glyph: "return-card", key: "", group: 1, tip: "Reset deck and card review state after confirmation; card content stays intact." },
        { title: "Checkpoint", glyph: "pin", key: "", group: 2, tip: "Save a named deck checkpoint. This feature is planned." },
        { title: "Restore checkpoint", glyph: "clock", key: "", group: 2, tip: "Choose a known checkpoint to restore. This feature is planned." },
        { title: "Delete deck", glyph: "close", key: "Del", group: 3, danger: true, tip: "Remove the deck after confirmation." }
    ]
    readonly property var actions: deckMode ? deckActions : cardActions

    function reportAction(variant, action) {
        actionStatus = designs[variant].name + " / " + action.title + " selected. Preview only."
    }

    component MenuSurface: FocusScope {
        id: surface
        required property int variant
        required property var actions
        required property string heading
        property int previewIndex: 1
        property int hoveredIndex: -1
        readonly property int inset: variant === 1 ? 10 : 1
        readonly property int rowHeight: variant === 1 ? 48 : 44
        readonly property int corner: variant === 1 ? 14 : 7
        readonly property int groupGap: variant === 0 ? 9 : 12
        readonly property int titleHeight: 42
        readonly property int menuHeight: titleHeight + actions.length * rowHeight + groupCount() * groupGap + inset + 5
        implicitHeight: menuHeight
        implicitWidth: 320
        Accessible.role: Accessible.PopupMenu
        Accessible.name: heading
        signal activated(var action)
        signal dismissed()

        function groupCount() {
            let count = 0
            for (let index = 1; index < actions.length; ++index)
                if (actions[index].group !== actions[index - 1].group) ++count
            return count
        }
        function focusEntry(index) {
            const row = entries.itemAt(Math.max(0, Math.min(actions.length - 1, index)))
            if (row) row.focusEntry()
        }
        function focusedEntry() {
            for (let index = 0; index < entries.count; ++index) {
                const row = entries.itemAt(index)
                if (row && row.entryFocused) return index
            }
            return -1
        }
        Keys.onPressed: function(event) {
            switch (event.key) {
            case Qt.Key_Down:
                focusEntry((focusedEntry() + 1) % actions.length)
                event.accepted = true
                break
            case Qt.Key_Up:
                focusEntry((focusedEntry() + actions.length - 1) % actions.length)
                event.accepted = true
                break
            case Qt.Key_Home:
                focusEntry(0)
                event.accepted = true
                break
            case Qt.Key_End:
                focusEntry(actions.length - 1)
                event.accepted = true
                break
            case Qt.Key_Escape:
                dismissed()
                event.accepted = true
                break
            }
        }
        Rectangle {
            anchors.fill: parent
            anchors.topMargin: 4
            anchors.leftMargin: 2
            anchors.rightMargin: -2
            anchors.bottomMargin: -4
            radius: surface.corner
            color: Theme.scrim
            opacity: 0.5
        }
        Rectangle {
            anchors.fill: parent
            color: surface.variant === 0 ? Theme.ledgerSurface : Theme.surfaceRaised
            radius: surface.corner
            border.color: surface.variant === 0 ? Theme.ledgerRule : Theme.rule
            border.width: 1
        }
        Label {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: 17
            anchors.rightMargin: 17
            height: surface.titleHeight
            text: surface.heading
            textFormat: Text.PlainText
            verticalAlignment: Text.AlignVCenter
            color: Theme.inkMuted
            font.family: Theme.monoFont
            font.pixelSize: 11
            elide: Text.ElideRight
        }
        Rectangle {
            visible: surface.variant === 0
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 1
            anchors.rightMargin: 1
            y: surface.titleHeight - 1
            height: 1
            color: Theme.ledgerRule
        }
        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: surface.inset
            anchors.rightMargin: surface.inset
            anchors.topMargin: surface.titleHeight
            Repeater {
                id: entries
                model: surface.actions
                delegate: Item {
                    id: entry
                    required property var modelData
                    required property int index
                    readonly property bool beginsGroup: index > 0 && modelData.group !== surface.actions[index - 1].group
                    readonly property int separatorHeight: beginsGroup ? surface.groupGap : 0
                    readonly property bool entryFocused: actionButton.activeFocus
                    width: parent.width
                    height: surface.rowHeight + separatorHeight
                    function focusEntry() { actionButton.forceActiveFocus() }
                    Rectangle {
                        visible: entry.beginsGroup
                        y: Math.floor(entry.separatorHeight / 2)
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: surface.variant === 2 ? 16 : 9
                        anchors.rightMargin: surface.variant === 2 ? 16 : 9
                        height: 1
                        color: Theme.rule
                    }
                    Button {
                        id: actionButton
                        anchors.left: parent.left
                        anchors.right: parent.right
                        y: entry.separatorHeight
                        height: surface.rowHeight
                        leftPadding: surface.variant === 2 ? 17 : 14
                        rightPadding: surface.variant === 1 ? 12 : 15
                        activeFocusOnTab: true
                        readonly property bool emphasized: surface.hoveredIndex >= 0 ? surface.hoveredIndex === entry.index
                            : surface.activeFocus ? activeFocus : surface.previewIndex === entry.index
                        readonly property color actionInk: entry.modelData.danger ? Theme.error : Theme.ink
                        text: entry.modelData.title
                        Accessible.role: Accessible.MenuItem
                        Accessible.name: text
                        ToolTip.visible: hovered
                        ToolTip.delay: 500
                        ToolTip.text: entry.modelData.tip + (entry.modelData.key ? " Proposed shortcut: " + entry.modelData.key + "." : "")
                        onHoveredChanged: {
                            if (hovered) surface.hoveredIndex = entry.index
                            else if (surface.hoveredIndex === entry.index) surface.hoveredIndex = -1
                        }
                        onClicked: surface.activated(entry.modelData)
                        contentItem: RowLayout {
                            spacing: surface.variant === 1 ? 12 : 10
                            Icon {
                                kind: entry.modelData.glyph
                                Layout.preferredWidth: surface.variant === 1 ? 21 : 19
                                Layout.preferredHeight: surface.variant === 1 ? 21 : 19
                                stroke: actionButton.actionInk
                                lineWidth: 1.4
                            }
                            Label {
                                Layout.fillWidth: true
                                text: entry.modelData.title
                                textFormat: Text.PlainText
                                color: actionButton.actionInk
                                font.family: Theme.uiFont
                                font.pixelSize: surface.variant === 1 ? 15 : 14
                                font.weight: actionButton.emphasized ? Font.DemiBold : Font.Normal
                                elide: Text.ElideRight
                            }
                            Label {
                                text: entry.modelData.key
                                color: Theme.inkMuted
                                font.family: Theme.monoFont
                                font.pixelSize: 10
                                visible: text.length > 0
                            }
                        }
                        background: Item {
                            Rectangle {
                                anchors.fill: parent
                                anchors.topMargin: surface.variant === 1 ? 2 : 0
                                anchors.bottomMargin: surface.variant === 1 ? 2 : 0
                                radius: surface.variant === 1 ? 8 : 0
                                color: actionButton.emphasized && surface.variant !== 2 ? Theme.accentSoft : Theme.transparent
                                border.width: actionButton.activeFocus && surface.variant === 1 ? 1 : 0
                                border.color: Theme.accent
                            }
                            Rectangle {
                                visible: actionButton.emphasized && surface.variant !== 1
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                anchors.topMargin: surface.variant === 2 ? 10 : 0
                                anchors.bottomMargin: surface.variant === 2 ? 10 : 0
                                width: surface.variant === 2 ? 3 : 2
                                radius: surface.variant === 2 ? 1.5 : 0
                                color: entry.modelData.danger ? Theme.error : Theme.accent
                            }
                            Rectangle {
                                visible: surface.variant === 0 && entry.index + 1 < surface.actions.length && surface.actions[entry.index + 1].group === entry.modelData.group
                                anchors.bottom: parent.bottom
                                anchors.left: parent.left
                                anchors.right: parent.right
                                height: 1
                                color: Theme.rule
                            }
                        }
                    }
                }
            }
        }
    }

    component StudyPopup: Popup {
        id: popup
        required property int variant
        required property var actions
        required property string heading
        padding: 0
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        implicitHeight: popupSurface.implicitHeight
        width: 330
        background: Item {}
        onOpened: Qt.callLater(function () { popupSurface.focusEntry(0) })
        signal activated(var action)
        contentItem: MenuSurface {
            id: popupSurface
            variant: popup.variant
            actions: popup.actions
            heading: popup.heading
            previewIndex: -1
            onActivated: function(action) { popup.activated(action); popup.close() }
            onDismissed: popup.close()
        }
    }

    header: Rectangle {
        height: 78
        color: Theme.canvas
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.rule }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 28
            anchors.rightMargin: 28
            spacing: 16
            Label { text: "Menu studies"; color: Theme.ink; font.pixelSize: 22; font.weight: Font.DemiBold }
            Item { Layout.fillWidth: true }
            RowLayout {
                spacing: 0
                Repeater {
                    model: ["Card", "Deck"]
                    delegate: Button {
                        id: modeButton
                        required property string modelData
                        required property int index
                        implicitWidth: 88
                        implicitHeight: 36
                        text: modelData
                        readonly property bool selected: gallery.deckMode === (index === 1)
                        ToolTip.visible: hovered
                        ToolTip.text: "Compare " + modelData.toLowerCase() + " action menus"
                        onClicked: gallery.deckMode = index === 1
                        contentItem: Label {
                            text: modeButton.text
                            color: modeButton.selected ? Theme.editorTabActiveInk : Theme.inkMuted
                            font.family: Theme.uiFont; font.pixelSize: 12
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: 5
                            color: modeButton.selected ? Theme.editorTabActive : Theme.editorTabInactive
                            border.width: modeButton.activeFocus ? 2 : 0
                            border.color: Theme.accent
                        }
                    }
                }
            }
            GlyphButton {
                glyph: Theme.dark ? "sun" : "moon"
                hint: Theme.dark ? "Switch to light theme" : "Switch to dark theme"
                onClicked: Theme.toggle()
            }
        }
    }

    footer: Rectangle {
        height: 50
        color: Theme.canvas
        Rectangle { width: parent.width; height: 1; color: Theme.rule }
        Label {
            anchors.fill: parent
            anchors.leftMargin: 28
            anchors.rightMargin: 28
            text: gallery.actionStatus
            textFormat: Text.PlainText
            color: Theme.inkMuted
            font.pixelSize: 12
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 18
        Label {
            Layout.fillWidth: true
            text: "Compare the rows below, then open a menu with the three dots or right-click the sample. Keyboard hints are proposals."
            textFormat: Text.PlainText
            color: Theme.inkMuted
            font.pixelSize: 12
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 24
            Repeater {
                model: gallery.designs
                delegate: ColumnLayout {
                    id: study
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.minimumWidth: 0
                    Layout.fillHeight: true
                    spacing: 10
                    Label {
                        Layout.fillWidth: true
                        text: "0" + (study.index + 1) + "  " + study.modelData.name
                        color: Theme.ink
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 28
                        text: study.modelData.description
                        color: Theme.inkMuted
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }
                    Rectangle {
                        id: sample
                        Layout.fillWidth: true
                        Layout.preferredHeight: 62
                        color: Theme.surface
                        border.color: Theme.rule
                        radius: 7
                        function openMenu(point) {
                            const target = sample.mapToItem(gallery.contentItem, point.x, point.y)
                            actualMenu.x = Math.max(8, Math.min(target.x, gallery.contentItem.width - actualMenu.width - 8))
                            actualMenu.y = Math.max(8, Math.min(target.y, gallery.contentItem.height - actualMenu.implicitHeight - 8))
                            actualMenu.open()
                        }
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.RightButton
                            onClicked: function(mouse) { sample.openMenu(Qt.point(mouse.x, mouse.y)) }
                        }
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 10
                            Icon { kind: gallery.deckMode ? "folder" : "cards"; Layout.preferredWidth: 20; Layout.preferredHeight: 20; stroke: Theme.inkMuted }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                Label { Layout.fillWidth: true; text: gallery.deckMode ? "C++ examples" : "What is a pure function?"; font.pixelSize: 11; color: Theme.ink; elide: Text.ElideRight }
                                Label { Layout.fillWidth: true; text: gallery.deckMode ? "Sample deck" : "Sample card"; font.pixelSize: 10; color: Theme.inkMuted; elide: Text.ElideRight }
                            }
                            GlyphButton {
                                glyph: "more"
                                hint: "Open " + study.modelData.name.toLowerCase() + " menu"
                                implicitWidth: 32
                                implicitHeight: 32
                                onClicked: sample.openMenu(Qt.point(sample.width - actualMenu.width, sample.height + 6))
                            }
                        }
                        StudyPopup {
                            id: actualMenu
                            parent: gallery.contentItem
                            variant: study.index
                            actions: gallery.actions
                            heading: gallery.deckMode ? "C++ examples" : "Card actions"
                            width: Math.max(260, Math.min(350, sample.width))
                            onActivated: function(action) { gallery.reportAction(study.index, action) }
                        }
                    }
                    MenuSurface {
                        Layout.fillWidth: true
                        Layout.preferredHeight: implicitHeight
                        variant: study.index
                        actions: gallery.actions
                        heading: gallery.deckMode ? "C++ examples" : "Card actions"
                        onActivated: function(action) { gallery.reportAction(study.index, action) }
                        onDismissed: gallery.contentItem.forceActiveFocus()
                    }
                    Item { Layout.fillHeight: true; Layout.minimumHeight: 0 }
                }
            }
        }
    }
}
