pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore

Item {
    id: library
    required property var ui
    property string deckSearchText: ""
    property bool leftOpen: librarySettings.leftOpen
    property bool rightOpen: librarySettings.rightOpen && !!ui.selectedDeck.id
    readonly property bool effectiveLeftOpen: leftOpen && (!rightOpen || width >= 720)
    property real splitRatio: librarySettings.splitRatio
    property var expandedDecks: ({})
    property bool expansionInitialized: false
    property bool deckSearchVisible: false
    property bool cardSearchVisible: false
    readonly property var visibleCards: app.cards.filter(card => card.deckId === ui.selectedDeck.id
        && (!ui.cardSearchText || (String(card.front) + " " + String(card.back) + " " + String(card.tags)).toLowerCase().indexOf(ui.cardSearchText.toLowerCase()) >= 0))
    readonly property var treeRows: flattenTree()

    Settings {
        id: librarySettings
        category: "libraryFolders"
        property bool leftOpen: true
        property bool rightOpen: false
        property real splitRatio: 0.5
        property string expanded: ""
        property real treeScrollY: 0
        property real cardScrollY: 0
    }

    Component.onCompleted: {
        library.initializeExpansion()
        Qt.callLater(function () {
            if (library.treeRows.length > 0)
                treeList.forceActiveFocus()
            treeList.contentY = librarySettings.treeScrollY
            cardList.contentY = librarySettings.cardScrollY
        })
    }
    onExpandedDecksChanged: {
        const ids = Object.keys(expandedDecks).filter(id => expandedDecks[id])
        librarySettings.expanded = JSON.stringify(ids)
    }
    onLeftOpenChanged: librarySettings.leftOpen = leftOpen
    onRightOpenChanged: librarySettings.rightOpen = rightOpen
    onSplitRatioChanged: librarySettings.splitRatio = splitRatio

    function childrenOf(id) {
        return app.decks.filter(deck => (deck.parentId || "") === id).sort((a, b) => String(a.name).localeCompare(String(b.name)))
    }
    function initializeExpansion() {
        if (expansionInitialized || app.decks.length === 0)
            return
        const state = ({})
        try {
            const saved = JSON.parse(librarySettings.expanded || "")
            for (const id of saved)
                state[id] = true
        } catch (error) {
            for (const root of childrenOf(""))
                state[root.id] = true
        }
        if (librarySettings.expanded.length === 0)
            for (const root of childrenOf(""))
                state[root.id] = true
        expandedDecks = state
        expansionInitialized = true
    }
    function hasMatchingDescendant(id, query) {
        return childrenOf(id).some(child => String(child.name).toLowerCase().indexOf(query) >= 0 || hasMatchingDescendant(child.id, query))
    }
    function flattenTree() {
        const rows = []
        const query = deckSearchText.trim().toLowerCase()
        function append(deck, depth) {
            const children = library.childrenOf(deck.id)
            const directMatch = !query || String(deck.name).toLowerCase().indexOf(query) >= 0
            const descendantMatch = query && library.hasMatchingDescendant(deck.id, query)
            if (query && !directMatch && !descendantMatch)
                return
            const expanded = query || !!library.expandedDecks[deck.id]
            rows.push({ deck: deck, id: deck.id, depth: depth, expanded: expanded, hasChildren: children.length > 0 })
            if (expanded)
                for (const child of children)
                    append(child, depth + 1)
        }
        for (const root of library.childrenOf(""))
            append(root, 0)
        return rows
    }
    function toggleDeck(id) {
        const state = Object.assign({}, expandedDecks)
        state[id] = !state[id]
        expandedDecks = state
    }
    function revealDeck(id) {
        let deck = ui.deckById(id)
        const state = Object.assign({}, expandedDecks)
        while (deck && deck.parentId) {
            state[deck.parentId] = true
            deck = ui.deckById(deck.parentId)
        }
        expandedDecks = state
    }
    function showDeck(id, focusCardsAfter) {
        if (!id)
            return
        ui.setDeck(id, true)
        rightOpen = true
        if (focusCardsAfter)
            Qt.callLater(function () { if (cardList.count > 0) cardList.forceActiveFocus() })
    }
    function chooseTreeRow(index) {
        const row = treeRows[index]
        if (!row)
            return
        treeList.currentIndex = index
        showDeck(row.id, false)
        treeList.forceActiveFocus()
    }
    function chooseCard(index) {
        const card = visibleCards[index]
        if (!card)
            return
        cardList.currentIndex = index
        ui.selectedCardId = card.id
    }
    function openCard(card) {
        if (!card)
            return
        ui.selectedCardId = card.id
        ui.openSelectedDetail()
    }
    function restoreScroll(value) {
        cardList.contentY = Math.max(0, Math.min(Number(value) || 0, Math.max(0, cardList.contentHeight - cardList.height)))
    }
    function focusCards() {
        if (!rightOpen)
            return
        cardList.forceActiveFocus()
        if (cardList.currentIndex < 0 && cardList.count > 0)
            cardList.currentIndex = Math.max(0, visibleCards.findIndex(card => card.id === ui.selectedCardId))
    }
    function focusDeckSearch() {
        if (!effectiveLeftOpen)
            leftOpen = true
        deckSearchVisible = true
        deckSearch.forceActiveFocus()
        deckSearch.selectAll()
    }

    Connections {
        target: app
        function onDecksChanged() {
            library.initializeExpansion()
            if (ui.selectedDeck.id)
                library.revealDeck(ui.selectedDeck.id)
        }
        function onSelectedDeckIdChanged() { if (ui.selectedDeck.id) library.revealDeck(ui.selectedDeck.id) }
    }

    RowLayout {
        id: libraryRow
        anchors.fill: parent
        anchors.margins: library.ui.gutter
        spacing: 0
        Rectangle {
            id: deckPane
            objectName: "libraryDeckPane"
            Layout.fillHeight: true
            Layout.fillWidth: !library.rightOpen || !library.effectiveLeftOpen
            Layout.preferredWidth: library.effectiveLeftOpen ? (library.rightOpen ? libraryRow.width * library.splitRatio : 0) : 48
            Layout.minimumWidth: library.effectiveLeftOpen ? (library.rightOpen ? 300 : 260) : 48
            color: Theme.surface
            border.color: Theme.rule
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: library.effectiveLeftOpen ? 16 : 4
                spacing: 10
                visible: library.effectiveLeftOpen
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 42
                    Label { text: "Decks"; color: Theme.ink; font.family: Theme.uiFont; font.pixelSize: 20; font.weight: Font.DemiBold; Layout.fillWidth: true }
                    GlyphButton { glyph: "search"; hint: "Find a deck (" + shortcuts.bindings.deckSearch + ")"; onClicked: library.focusDeckSearch() }
                    GlyphButton {
                        glyph: "more"; hint: "Deck actions"; onClicked: deckMenu.open()
                        Menu {
                            id: deckMenu; y: parent.height
                            MenuItem { text: "New deck"; onTriggered: ui.openDeckEditor("") }
                            MenuItem { text: "New child deck"; enabled: !!ui.selectedDeck.id; onTriggered: ui.openDeckEditor("", ui.selectedDeck.id) }
                            MenuItem { text: "Edit deck"; enabled: !!ui.selectedDeck.id; onTriggered: ui.openDeckEditor(ui.selectedDeck.id) }
                            MenuItem { text: "Review this deck"; enabled: !!ui.selectedDeck.id; onTriggered: { ui.page = 0; if (!app.reviewing) app.startReview(ui.selectedDeck.id) } }
                            MenuItem { text: "Reset deck"; visible: app.developmentToolsEnabled; enabled: !app.busy && !!ui.selectedDeck.id; onTriggered: ui.confirmReset("deck", ui.selectedDeck.id, ui.selectedDeck.name) }
                            MenuSeparator { visible: !!ui.selectedDeck.id }
                            MenuItem { text: "Delete deck"; enabled: !!ui.selectedDeck.id; onTriggered: ui.confirmDelete("deck", ui.selectedDeck.id, ui.selectedDeck.name) }
                        }
                    }
                    PaneToggleButton { hint: "Collapse deck pane"; onClicked: library.leftOpen = false }
                }
                TextField {
                    id: deckSearch; objectName: "deckSearchField"; visible: library.deckSearchVisible
                    Layout.fillWidth: true; placeholderText: "Find a deck"; text: library.deckSearchText
                    onTextEdited: library.deckSearchText = text
                    Keys.onEscapePressed: { library.deckSearchText = ""; library.deckSearchVisible = false; treeList.forceActiveFocus() }
                }
                ListView {
                    id: treeList; objectName: "libraryDeckTree"; Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                    model: library.treeRows; keyNavigationEnabled: true; boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    onContentYChanged: librarySettings.treeScrollY = contentY
                    delegate: Item {
                        id: treeRow; required property var modelData; required property int index
                        width: treeList.width; height: 50
                        Rectangle {
                            anchors.fill: parent; anchors.leftMargin: 4 + treeRow.modelData.depth * 20; anchors.rightMargin: 4; radius: 5
                            color: ui.selectedDeck.id === treeRow.modelData.id ? Theme.accentSoft : (treeMouse.containsMouse ? Theme.surfaceRaised : Theme.transparent)
                            border.width: ui.selectedDeck.id === treeRow.modelData.id || treeList.activeFocus && treeList.currentIndex === treeRow.index ? 2 : 0
                            border.color: ui.selectedDeck.id === treeRow.modelData.id ? Theme.accent : Theme.rule
                        }
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: 10 + treeRow.modelData.depth * 20; anchors.rightMargin: 12; spacing: 9
                            GlyphButton {
                                visible: treeRow.modelData.hasChildren; glyph: treeRow.modelData.expanded ? "down" : "chevron"; glyphSize: 15; implicitWidth: 26; implicitHeight: 30
                                hint: treeRow.modelData.expanded ? "Collapse deck" : "Expand deck"; onClicked: library.toggleDeck(treeRow.modelData.id)
                            }
                            Item { visible: !treeRow.modelData.hasChildren; implicitWidth: 26; implicitHeight: 30 }
                            Icon { kind: treeRow.modelData.expanded ? "folder-open" : "folder"; width: 22; height: 22; stroke: ui.selectedDeck.id === treeRow.modelData.id ? Theme.accent : Theme.inkMuted }
                            Label { Layout.fillWidth: true; textFormat: Text.PlainText; text: treeRow.modelData.deck.name; color: Theme.ink; font.family: Theme.contentFont; font.pixelSize: 14; elide: Text.ElideRight }
                            Label { visible: Number(treeRow.modelData.deck.ownCardCount || 0) > 0; text: String(treeRow.modelData.deck.ownCardCount || 0); color: Theme.inkMuted; font.family: Theme.monoFont; font.pixelSize: 11 }
                        }
                        MouseArea {
                            id: treeMouse
                            anchors.top: parent.top
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.leftMargin: 42 + treeRow.modelData.depth * 20
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton
                            onClicked: library.chooseTreeRow(treeRow.index)
                        }
                    }
                    Label {
                        visible: treeList.count === 0 && !library.deckSearchText
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.verticalCenter: parent.verticalCenter
                        text: "No decks yet"
                        color: Theme.inkMuted
                    }
                    AppButton {
                        visible: treeList.count === 0 && !library.deckSearchText
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.verticalCenter
                        anchors.topMargin: 22
                        text: "New deck"
                        primary: true
                        onClicked: ui.openDeckEditor("")
                    }
                    Keys.onPressed: function(event) {
                        const row = library.treeRows[currentIndex]
                        if (!row) return
                        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) { library.chooseTreeRow(currentIndex); event.accepted = true }
                        else if (event.key === Qt.Key_Right && row.hasChildren) { if (!row.expanded) library.toggleDeck(row.id); else currentIndex = Math.min(count - 1, currentIndex + 1); event.accepted = true }
                        else if (event.key === Qt.Key_Left) {
                            if (row.expanded && row.hasChildren)
                                library.toggleDeck(row.id)
                            else if (row.depth > 0) {
                                let parentIndex = currentIndex - 1
                                while (parentIndex >= 0 && library.treeRows[parentIndex].depth >= row.depth)
                                    --parentIndex
                                if (parentIndex >= 0)
                                    currentIndex = parentIndex
                            }
                            event.accepted = true
                        } else if (event.key === Qt.Key_Up) {
                            currentIndex = Math.max(0, currentIndex - 1)
                            event.accepted = true
                        } else if (event.key === Qt.Key_Down) {
                            currentIndex = Math.min(count - 1, currentIndex + 1)
                            event.accepted = true
                        }
                    }
                }
            }
            PaneToggleButton { visible: !library.effectiveLeftOpen; anchors.top: parent.top; anchors.left: parent.left; anchors.margins: 4; collapsed: true; hint: "Expand deck pane"; onClicked: library.leftOpen = true }
        }
        Item {
            id: splitHandle; visible: library.effectiveLeftOpen && library.rightOpen; Layout.preferredWidth: visible ? 12 : 0; Layout.fillHeight: true; activeFocusOnTab: true
            Rectangle { width: splitHandle.activeFocus ? 3 : 1; height: 56; anchors.centerIn: parent; color: splitHandle.activeFocus ? Theme.accent : Theme.rule }
            Accessible.name: "Resize library panes. Left and right arrows adjust width; Home restores equal halves."
            Keys.onLeftPressed: library.splitRatio = Math.max(0.3, library.splitRatio - 0.03)
            Keys.onRightPressed: library.splitRatio = Math.min(0.7, library.splitRatio + 0.03)
            Keys.onHomePressed: library.splitRatio = 0.5
            MouseArea {
                anchors.fill: parent; cursorShape: Qt.SizeHorCursor; property real startX: 0; property real startRatio: 0.5
                onPressed: function(mouse) { splitHandle.forceActiveFocus(); startX = mapToItem(libraryRow, mouse.x, mouse.y).x; startRatio = library.splitRatio }
                onPositionChanged: function(mouse) {
                    if (pressed && libraryRow.width > 0)
                        library.splitRatio = Math.max(0.3, Math.min(0.7, startRatio + (mapToItem(libraryRow, mouse.x, mouse.y).x - startX) / libraryRow.width))
                }
            }
        }
        Rectangle {
            id: cardPane; objectName: "libraryCardPane"; visible: library.rightOpen; Layout.fillWidth: true; Layout.fillHeight: true; color: Theme.surface; border.color: Theme.rule
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 16; spacing: 12
                RowLayout {
                    Layout.fillWidth: true; Layout.preferredHeight: 42
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 2
                        Label { text: "Cards"; color: Theme.ink; font.family: Theme.uiFont; font.pixelSize: 20; font.weight: Font.DemiBold }
                        Label { text: ui.selectedDeck.name + " · " + library.visibleCards.length + " cards"; color: Theme.inkMuted; font.family: Theme.monoFont; font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true }
                    }
                    GlyphButton { glyph: "plus"; hint: "Add card"; onClicked: ui.openCardEditor("") }
                    GlyphButton { glyph: "search"; hint: "Find a card"; onClicked: { library.cardSearchVisible = true; cardSearch.forceActiveFocus() } }
                    PaneToggleButton { rightSide: true; hint: "Close card pane"; onClicked: library.rightOpen = false }
                }
                TextField {
                    id: cardSearch; objectName: "libraryCardSearch"; Layout.fillWidth: true; visible: library.cardSearchVisible
                    placeholderText: "Find a card by content or tag"; text: ui.cardSearchText; onTextEdited: ui.cardSearchText = text
                    Keys.onEscapePressed: { ui.cardSearchText = ""; library.cardSearchVisible = false; library.focusCards() }
                }
                ListView {
                    id: cardList; objectName: "libraryCardList"; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: library.visibleCards; currentIndex: -1; keyNavigationEnabled: true; boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    onContentYChanged: if (ui.page === 1 && !ui.restoringDeck) { ui.libraryScrollY = Math.max(0, contentY); librarySettings.cardScrollY = Math.max(0, contentY) }
                    Component.onCompleted: Qt.callLater(function() { library.restoreScroll(ui.libraryScrollY) })
                    delegate: ItemDelegate {
                        id: cardRow; required property var modelData; required property int index; width: cardList.width; height: 82
                        highlighted: ui.selectedCardId === cardRow.modelData.id || cardList.activeFocus && cardList.currentIndex === cardRow.index
                        onClicked: library.chooseCard(cardRow.index)
                        onDoubleClicked: library.openCard(cardRow.modelData)
                        contentItem: ColumnLayout {
                            spacing: 5
                            Label { textFormat: Text.PlainText; text: ui.compactText(cardRow.modelData.front); color: Theme.ink; font.family: Theme.contentFont; font.pixelSize: 16; elide: Text.ElideRight; Layout.fillWidth: true }
                            RowLayout {
                                Layout.fillWidth: true
                                Label {
                                    text: cardRow.modelData.kind === "reverse" ? "Both directions" : cardRow.modelData.kind === "cloze" ? "Cloze" : "Basic"
                                    color: Theme.inkMuted
                                    font.family: Theme.monoFont
                                    font.pixelSize: 10
                                }
                                Item { Layout.fillWidth: true }
                                Label {
                                    textFormat: Text.PlainText
                                    text: cardRow.modelData.tags || ""
                                    color: Theme.inkMuted
                                    font.family: Theme.monoFont
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                    Layout.maximumWidth: cardRow.width * 0.45
                                }
                            }
                        }
                        background: Rectangle { color: cardRow.highlighted || cardRow.hovered ? Theme.accentSoft : Theme.transparent; border.width: cardRow.highlighted ? 2 : 0; border.color: Theme.accent; Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.rule } }
                    }
                    Keys.onReturnPressed: library.openCard(visibleCards[currentIndex])
                    Keys.onEnterPressed: library.openCard(visibleCards[currentIndex])
                    onCurrentIndexChanged: if (activeFocus && visibleCards[currentIndex]) ui.selectedCardId = visibleCards[currentIndex].id
                    Label { visible: cardList.count === 0; anchors.centerIn: parent; text: ui.cardSearchText ? "No cards match this search." : "This deck has no cards yet."; color: Theme.inkMuted; wrapMode: Text.Wrap }
                }
            }
        }
        PaneToggleButton {
            visible: !library.rightOpen && !!ui.selectedDeck.id; Layout.preferredWidth: visible ? 48 : 0; Layout.fillHeight: true; collapsed: true; rightSide: true
            hint: "Open card pane"; onClicked: library.rightOpen = true
        }
    }
}
