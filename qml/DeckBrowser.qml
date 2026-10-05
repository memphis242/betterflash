import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: browser
    required property var ui
    objectName: "reviewDeckBrowser"
    spacing: 10
    readonly property var levelDecks: app.decks.filter(deck => (deck.parentId || "") === ui.deckBrowserParentId)
    readonly property var levelDeck: ui.deckById(ui.deckBrowserParentId)
    readonly property var selected: ui.selectedDeck
    property string treeRootId: ""
    property var openBranches: ({})
    readonly property var treeRows: flattenTree(treeRootId)

    function children(id) {
        return app.decks.filter(deck => (deck.parentId || "") === id)
    }
    function enterDeck(id) {
        if (!children(id).length)
            return
        ui.deckBrowserParentId = id
        deckList.currentIndex = 0
    }
    function leaveLevel() {
        if (ui.deckBrowserParentId)
            ui.deckBrowserParentId = levelDeck.parentId || ""
    }
    function chooseIndex(index) {
        if (index < 0 || index >= levelDecks.length)
            return
        deckList.currentIndex = index
        ui.setDeck(levelDecks[index].id, true)
    }
    function showSelection() {
        const index = levelDecks.findIndex(deck => deck.id === app.selectedDeckId)
        if (index >= 0) {
            deckList.currentIndex = index
            deckList.positionViewAtIndex(index, ListView.Contain)
        }
    }
    onLevelDecksChanged: Qt.callLater(showSelection)
    function toggleBranch(id) {
        const branches = Object.assign({}, openBranches)
        branches[id] = !branches[id]
        openBranches = branches
    }
    function flattenTree(rootId) {
        const rows = []
        const visited = ({})
        const decks = app.decks
        const byId = ({})
        const byParent = ({})
        for (const deck of decks) {
            byId[deck.id] = deck
            const parent = deck.parentId || ""
            if (!byParent[parent])
                byParent[parent] = []
            byParent[parent].push(deck)
        }
        const pending = rootId && byId[rootId] ? [{ id: rootId, depth: 0 }] : []
        while (pending.length) {
            const item = pending.pop()
            if (visited[item.id])
                continue
            visited[item.id] = true
            const deck = byId[item.id]
            const descendants = byParent[item.id] || []
            const expanded = !!openBranches[item.id]
            rows.push({ id: deck.id, name: deck.name, depth: item.depth,
                        dueCount: deck.dueCount || 0, hasChildren: descendants.length > 0, expanded: expanded })
            if (expanded)
                for (let i = descendants.length - 1; i >= 0; --i)
                    pending.push({ id: descendants[i].id, depth: item.depth + 1 })
        }
        return rows
    }
    function resetTreeRoot() {
        treeRootId = app.selectedDeckId
        const branches = ({})
        if (treeRootId)
            branches[treeRootId] = true
        openBranches = branches
    }
    Connections {
        target: app
        function onSelectedDeckIdChanged() {
            // Keep the open tree stable while selecting a row inside it.
            if (!ui.deckTreeExpanded || !browser.treeRows.some(row => row.id === app.selectedDeckId))
                browser.resetTreeRoot()
            Qt.callLater(browser.showSelection)
        }
        function onDecksChanged() {
            if (ui.deckBrowserParentId && !ui.deckById(ui.deckBrowserParentId).id)
                ui.deckBrowserParentId = ""
            if (browser.treeRootId && !ui.deckById(browser.treeRootId).id)
                browser.resetTreeRoot()
        }
    }
    Component.onCompleted: {
        resetTreeRoot()
        Qt.callLater(showSelection)
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        GlyphButton {
            objectName: "reviewDeckBrowserBack"
            visible: !!browser.ui.deckBrowserParentId
            glyph: "arrow-left"
            hint: "Browse the parent deck level"
            onClicked: browser.leaveLevel()
        }
        GlyphButton {
            objectName: "reviewDeckBrowserRoot"
            visible: !!browser.ui.deckBrowserParentId
            glyph: "book"
            hint: "Show top-level decks"
            onClicked: browser.ui.deckBrowserParentId = ""
        }
        Label {
            objectName: "reviewDeckBreadcrumb"
            textFormat: Text.PlainText
            text: browser.levelDeck.name || "Decks"
            color: Theme.inkMuted
            elide: Text.ElideRight
            Layout.fillWidth: true
            Layout.minimumWidth: 0
        }
        GlyphButton {
            objectName: "reviewBrowseSubdecks"
            glyph: "arrow-right"
            enabled: !!browser.selected.id && browser.children(browser.selected.id).length > 0
            visible: enabled
            hint: "Browse subdecks of the selected deck (Down)"
            onClicked: browser.enterDeck(browser.selected.id)
        }
        GlyphButton {
            objectName: "reviewDeckTreeToggle"
            glyph: browser.ui.deckTreeExpanded ? "up" : "down"
            selected: browser.ui.deckTreeExpanded
            enabled: !!browser.selected.id && browser.children(browser.treeRootId || browser.selected.id).length > 0
            visible: enabled
            hint: browser.ui.deckTreeExpanded ? "Collapse the selected deck tree" : "Expand the selected deck tree"
            onClicked: {
                if (!browser.ui.deckTreeExpanded)
                    browser.resetTreeRoot()
                browser.ui.deckTreeExpanded = !browser.ui.deckTreeExpanded
                if (!browser.ui.deckTreeExpanded)
                    browser.resetTreeRoot()
            }
        }
        GlyphButton {
            glyph: "plus"
            hint: browser.ui.deckBrowserParentId ? "Create a deck at this level" : "Create a top-level deck"
            onClicked: browser.ui.openDeckEditor("", browser.ui.deckBrowserParentId)
        }
    }
    ListView {
        id: deckList
        objectName: "reviewDeckList"
        Layout.fillWidth: true
        Layout.preferredHeight: 140
        clip: true
        orientation: ListView.Horizontal
        model: browser.levelDecks
        spacing: 12
        boundsBehavior: Flickable.StopAtBounds
        keyNavigationEnabled: false
        Accessible.name: "Decks at this level"
        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
        Keys.onLeftPressed: browser.chooseIndex(Math.max(0, currentIndex - 1))
        Keys.onRightPressed: browser.chooseIndex(Math.min(count - 1, currentIndex + 1))
        Keys.onReturnPressed: browser.chooseIndex(currentIndex)
        Keys.onEnterPressed: browser.chooseIndex(currentIndex)
        Keys.onDownPressed: browser.enterDeck(app.selectedDeckId)
        Keys.onUpPressed: browser.leaveLevel()
        WheelHandler {
            target: null
            onWheel: event => {
                const delta = event.pixelDelta.x || event.pixelDelta.y || (event.angleDelta.x || event.angleDelta.y) / 2
                deckList.contentX = Math.max(0, Math.min(Math.max(0, deckList.contentWidth - deckList.width), deckList.contentX - delta))
                event.accepted = true
            }
        }
        delegate: Button {
            id: tile
            required property var modelData
            required property int index
            objectName: "reviewDeckTile" + index
            width: Math.min(270, deckList.width)
            height: 126
            padding: 16
            readonly property bool selected: app.selectedDeckId === modelData.id
            Accessible.name: modelData.name
            ToolTip.visible: hovered
            ToolTip.text: modelData.name + ": " + modelData.dueCount + " due, " + modelData.cardCount + " cards including subdecks"
            onClicked: {
                browser.chooseIndex(index)
                deckList.forceActiveFocus()
            }
            Keys.onLeftPressed: browser.chooseIndex(Math.max(0, index - 1))
            Keys.onRightPressed: browser.chooseIndex(Math.min(deckList.count - 1, index + 1))
            Keys.onDownPressed: browser.enterDeck(modelData.id)
            Keys.onUpPressed: browser.leaveLevel()
            TapHandler {
                gesturePolicy: TapHandler.WithinBounds
                onDoubleTapped: browser.enterDeck(tile.modelData.id)
            }
            contentItem: ColumnLayout {
                spacing: 10
                Label {
                    textFormat: Text.PlainText
                    text: tile.modelData.name
                    color: tile.selected ? Theme.accent : Theme.ink
                    font.pixelSize: 16
                    font.weight: Font.Medium
                    Layout.fillWidth: true
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    wrapMode: Text.Wrap
                }
                Item { Layout.fillHeight: true }
                Label {
                    textFormat: Text.PlainText
                    text: tile.modelData.dueCount + " due / " + tile.modelData.cardCount + " cards"
                    color: Theme.inkMuted
                    font.pixelSize: 11
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
            }
            background: Rectangle {
                color: tile.hovered ? Theme.surfaceRaised : Theme.surface
                radius: 4
                border.width: tile.selected || tile.activeFocus ? 2 : 1
                border.color: tile.selected || tile.activeFocus ? Theme.accent : Theme.rule
            }
        }
    }
    ListView {
        id: treeList
        objectName: "reviewDeckTree"
        visible: browser.ui.deckTreeExpanded && browser.children(browser.treeRootId).length > 0
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(contentHeight, browser.ui.width < 600 ? 112 : 180)
        model: browser.treeRows
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        delegate: RowLayout {
            id: treeRow
            required property var modelData
            required property int index
            width: treeList.width
            height: 42
            spacing: 4
            Item { Layout.preferredWidth: Math.min(treeRow.modelData.depth * 18, treeList.width / 3) }
            GlyphButton {
                objectName: "reviewDeckBranch" + treeRow.index
                glyph: treeRow.modelData.expanded ? "down" : "arrow-right"
                enabled: treeRow.modelData.hasChildren
                opacity: enabled ? 1 : 0
                hint: treeRow.modelData.expanded ? "Collapse subdecks" : "Expand subdecks"
                onClicked: browser.toggleBranch(treeRow.modelData.id)
            }
            AppButton {
                objectName: "reviewDeckTreeSelect" + treeRow.index
                text: treeRow.modelData.name
                Layout.fillWidth: true
                hint: treeRow.modelData.name + ": " + treeRow.modelData.dueCount + " due including subdecks"
                onClicked: browser.ui.setDeck(treeRow.modelData.id, true)
                Keys.onRightPressed: {
                    if (treeRow.modelData.hasChildren && !treeRow.modelData.expanded)
                        browser.toggleBranch(treeRow.modelData.id)
                }
                Keys.onLeftPressed: {
                    if (treeRow.modelData.expanded)
                        browser.toggleBranch(treeRow.modelData.id)
                }
                background: Rectangle {
                    radius: 4
                    color: parent.hovered ? Theme.surface : Theme.transparent
                    border.width: parent.activeFocus || app.selectedDeckId === treeRow.modelData.id ? 2 : 0
                    border.color: Theme.accent
                }
            }
            Label {
                textFormat: Text.PlainText
                text: treeRow.modelData.dueCount + " due"
                color: Theme.inkMuted
                font.pixelSize: 11
                Layout.preferredWidth: 76
            }
        }
    }
}
