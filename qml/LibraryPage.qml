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
    property bool rightOpen: librarySettings.rightOpen
    readonly property bool cardsVisible: rightOpen && !!ui.selectedDeck.id
    property bool restoringTree: true
    readonly property bool effectiveLeftOpen: leftOpen && (!cardsVisible || width >= 720)
    property real splitRatio: librarySettings.splitRatio
    property var expandedDecks: ({})
    property bool expansionInitialized: false
    property bool deckSearchVisible: false
    property bool cardSearchVisible: false
    property bool restoringSearchSelection: false
    property string searchSelectionId: ""
    property real searchSelectionScrollY: 0
    property var selectedCardIds: []
    property string cardSelectionAnchorId: ""
    readonly property var searchEngine: librarySearch
    readonly property bool searchingCards: ui.cardSearchText.trim().length > 0
    readonly property var visibleCards: library.searchingCards
        ? (library.searchEngine.results || [])
        : app.cards.filter(card => card.deckId === ui.selectedDeck.id)
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
        library.updateCardSearch()
        Qt.callLater(function () {
            if (library.treeRows.length > 0)
                treeList.forceActiveFocus()
            library.restoreScroll(ui.libraryScrollY)
        })
    }
    onExpandedDecksChanged: {
        if (expansionInitialized) {
            const ids = Object.keys(expandedDecks).filter(id => expandedDecks[id])
            librarySettings.expanded = JSON.stringify(ids)
        }
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
        librarySettings.expanded = JSON.stringify(Object.keys(state))
        const scrollY = librarySettings.treeScrollY
        Qt.callLater(function () {
            treeList.contentY = Math.max(0, Math.min(scrollY, treeList.contentHeight - treeList.height))
            library.restoringTree = false
        })
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
        revealDeck(id)
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
    function hydrateCardSelection() {
        if (selectedCardIds.length === 0 && ui.selectedCardId && visibleCards.some(card => card.id === ui.selectedCardId)) {
            selectedCardIds = [ui.selectedCardId]
            cardSelectionAnchorId = ui.selectedCardId
        }
    }
    function chooseCardWithModifiers(index, modifiers) {
        const card = visibleCards[index]
        if (!card)
            return
        const shift = (modifiers & Qt.ShiftModifier) !== 0
        const toggle = (modifiers & (Qt.ControlModifier | Qt.MetaModifier)) !== 0
        let ids = selectedCardIds.slice()
        if (shift && cardSelectionAnchorId) {
            const anchor = visibleCards.findIndex(item => item.id === cardSelectionAnchorId)
            if (anchor >= 0) {
                const first = Math.min(anchor, index)
                const last = Math.max(anchor, index)
                ids = visibleCards.slice(first, last + 1).map(item => item.id)
            } else {
                ids = [card.id]
            }
        } else if (toggle) {
            const existing = ids.indexOf(card.id)
            if (existing >= 0)
                ids.splice(existing, 1)
            else
                ids.push(card.id)
            if (!cardSelectionAnchorId)
                cardSelectionAnchorId = card.id
        } else {
            ids = [card.id]
            cardSelectionAnchorId = card.id
        }
        selectedCardIds = ids
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
        leftOpen = true
        if (width < 720) rightOpen = false
        deckSearchVisible = true
        deckSearch.forceActiveFocus()
        deckSearch.selectAll()
    }

    function updateCardSearch() {
        library.searchEngine.setActive(ui.page === 1)
        library.searchEngine.setSearch(ui.selectedDeck.id || "", ui.cardSearchText)
    }

    function selectCardAfterSearch() {
        const selectedIndex = library.visibleCards.findIndex(card => card.id === library.searchSelectionId)
        cardList.currentIndex = selectedIndex
        cardList.contentY = Math.max(0, Math.min(library.searchSelectionScrollY, Math.max(0, cardList.contentHeight - cardList.height)))
        library.restoringSearchSelection = false
    }

    Connections {
        target: app
        function onDecksChanged() {
            library.initializeExpansion()
            if (ui.selectedDeck.id)
                library.revealDeck(ui.selectedDeck.id)
        }
        function onSelectedDeckIdChanged() {
            library.selectedCardIds = []
            library.cardSelectionAnchorId = ""
            if (ui.selectedDeck.id) {
                library.revealDeck(ui.selectedDeck.id)
                Qt.callLater(library.hydrateCardSelection)
            }
        }
        function onCardsChanged() {
            const valid = new Set(app.cards.filter(card => card.deckId === ui.selectedDeck.id).map(card => card.id))
            library.selectedCardIds = library.selectedCardIds.filter(id => valid.has(id))
            if (library.cardSelectionAnchorId && !valid.has(library.cardSelectionAnchorId))
                library.cardSelectionAnchorId = ""
        }
    }

    Connections {
        target: library.searchEngine
        function onResultsAboutToChange() {
            if (!library.restoringSearchSelection) {
                library.searchSelectionId = ui.selectedCardId
                library.searchSelectionScrollY = cardList.contentY
            }
            library.restoringSearchSelection = true
        }
        function onResultsChanged() { Qt.callLater(library.selectCardAfterSearch) }
    }

    onVisibleChanged: updateCardSearch()
    Connections {
        target: ui
        function onSelectedDeckChanged() { library.updateCardSearch() }
        function onPageChanged() { library.updateCardSearch() }
        function onCardSearchTextChanged() { library.updateCardSearch() }
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
            Layout.fillWidth: library.effectiveLeftOpen && !library.cardsVisible
            Layout.preferredWidth: library.effectiveLeftOpen ? (library.cardsVisible ? (libraryRow.width - 12) * library.splitRatio : libraryRow.width - reopenCards.width) : 56
            Layout.minimumWidth: !library.effectiveLeftOpen ? 56 : library.cardsVisible ? 280 : 0
            Layout.maximumWidth: !library.effectiveLeftOpen ? 56 : library.cardsVisible ? Math.max(280, libraryRow.width - 312) : Infinity
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
                    Label { Layout.minimumWidth: 0; elide: Text.ElideRight; text: "Decks"; color: Theme.ink; font.family: Theme.uiFont; font.pixelSize: 24; font.weight: Font.DemiBold; Layout.fillWidth: true }
                    GlyphButton { glyph: "search"; hint: "Find a deck (" + shortcuts.bindings.deckSearch + ")"; onClicked: library.focusDeckSearch() }
                    GlyphButton {
                        glyph: "more"; hint: "Deck actions"; onClicked: deckMenu.open()
                        DeckActionsMenu { id: deckMenu; ui: library.ui; deckId: library.ui.selectedDeck.id; headerMode: true }
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
                    onContentYChanged: if (!library.restoringTree && library.visible && library.effectiveLeftOpen) librarySettings.treeScrollY = Math.max(0, contentY)
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
                                visible: treeRow.modelData.hasChildren; glyph: treeRow.modelData.expanded ? "down" : "chevron-right"; glyphSize: 15; implicitWidth: 26; implicitHeight: 30
                                hint: treeRow.modelData.expanded ? "Collapse deck" : "Expand deck"; onClicked: library.toggleDeck(treeRow.modelData.id)
                            }
                            Item { visible: !treeRow.modelData.hasChildren; implicitWidth: 26; implicitHeight: 30 }
                            Icon { kind: treeRow.modelData.expanded ? "folder-open" : "folder"; width: 22; height: 22; stroke: ui.selectedDeck.id === treeRow.modelData.id ? Theme.accent : Theme.inkMuted }
                            Label { Layout.fillWidth: true; textFormat: Text.PlainText; text: treeRow.modelData.deck.name; color: Theme.ink; font.family: Theme.contentFont; font.pixelSize: 17; elide: Text.ElideRight }
                            Label { visible: Number(treeRow.modelData.deck.ownCardCount || 0) > 0; text: String(treeRow.modelData.deck.ownCardCount || 0); color: Theme.inkMuted; font.family: Theme.monoFont; font.pixelSize: 11 }
                        }
                        MouseArea {
                            id: treeMouse
                            anchors.top: parent.top
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.left: parent.left
                            anchors.leftMargin: 42 + treeRow.modelData.depth * 20
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            onClicked: function(mouse) {
                                if (mouse.button === Qt.RightButton) {
                                    deckContextMenu.deckId = treeRow.modelData.id
                                    deckContextMenu.popup()
                                } else {
                                    library.chooseTreeRow(treeRow.index)
                                }
                            }
                        }
                        DeckActionsMenu {
                            id: deckContextMenu
                            ui: library.ui
                            deckId: treeRow.modelData.id
                            onBeforeAction: library.showDeck(deckId, false)
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
            PaneToggleButton { visible: !library.effectiveLeftOpen; anchors.top: parent.top; anchors.left: parent.left; anchors.margins: 2; collapsed: true; hint: "Expand deck pane"; onClicked: { library.leftOpen = true; if (library.width < 720) library.rightOpen = false } }
        }
        Item {
            id: splitHandle; visible: library.effectiveLeftOpen && library.cardsVisible; Layout.preferredWidth: visible ? 12 : 0; Layout.fillHeight: true; activeFocusOnTab: true
            Rectangle { width: splitHandle.activeFocus ? 3 : 1; height: 56; anchors.centerIn: parent; color: splitHandle.activeFocus ? Theme.accent : Theme.rule }
            Accessible.name: "Resize library panes. Left and right arrows adjust width; Home restores equal halves."
            Keys.onLeftPressed: library.splitRatio = Math.max(0.3, library.splitRatio - 0.03)
            Keys.onRightPressed: library.splitRatio = Math.min(0.7, library.splitRatio + 0.03)
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Home) {
                    library.splitRatio = 0.5
                    event.accepted = true
                }
            }
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
            id: cardPane; objectName: "libraryCardPane"; visible: library.cardsVisible; Layout.fillWidth: true; Layout.fillHeight: true; color: Theme.surface; border.color: Theme.rule
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 16; spacing: 12
                RowLayout {
                    Layout.fillWidth: true; Layout.preferredHeight: 42
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 2
                        Label { Layout.minimumWidth: 0; elide: Text.ElideRight; text: "Cards"; color: Theme.ink; font.family: Theme.uiFont; font.pixelSize: 24; font.weight: Font.DemiBold }
                        Label { textFormat: Text.PlainText; text: ui.selectedDeck.name + " / " + library.visibleCards.length + " cards"; color: Theme.inkMuted; font.family: Theme.monoFont; font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true }
                    }
                    ToolButton {
                        id: addCardButton
                        implicitWidth: 40
                        implicitHeight: 40
                        padding: 0
                        ToolTip.visible: hovered || activeFocus
                        ToolTip.text: "Add card"
                        Accessible.name: "Add card"
                        contentItem: Item {
                            anchors.fill: parent
                            Icon { kind: "plus"; width: 21; height: 21; anchors.centerIn: parent; stroke: Theme.onAccent; lineWidth: 1.8 }
                        }
                        background: Rectangle {
                            radius: width / 2
                            color: Theme.accent
                            border.width: addCardButton.activeFocus ? 2 : 0
                            border.color: addCardButton.hovered || addCardButton.down ? Theme.ink : Theme.transparent
                        }
                        onClicked: ui.openCardEditor("")
                    }
                    GlyphButton { glyph: "search"; hint: "Find a card"; onClicked: { library.cardSearchVisible = true; cardSearch.forceActiveFocus() } }
                    PaneToggleButton { rightSide: true; hint: "Close card pane"; onClicked: library.rightOpen = false }
                }
                TextField {
                    id: cardSearch; objectName: "libraryCardSearch"; Layout.fillWidth: true; visible: library.cardSearchVisible || ui.cardSearchText.length > 0
                    placeholderText: "Find a card by content, tag, or meaning"; maximumLength: 256; text: ui.cardSearchText; onTextEdited: ui.cardSearchText = text
                    Keys.onEscapePressed: { ui.cardSearchText = ""; library.updateCardSearch(); library.cardSearchVisible = false; library.focusCards() }
                }
                RowLayout {
                    visible: library.searchingCards && (library.searchEngine.busy || library.searchEngine.status.length > 0)
                    Layout.fillWidth: true
                    spacing: 8
                    BusyIndicator { running: library.searchEngine.busy; visible: running; implicitWidth: 18; implicitHeight: 18 }
                    Label {
                        Layout.fillWidth: true
                        textFormat: Text.PlainText
                        text: library.searchEngine.status
                        color: Theme.inkMuted
                        font.family: Theme.monoFont
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }
                ErrorBlock {
                    visible: library.searchingCards && library.searchEngine.error.length > 0
                    message: library.searchEngine.error
                    Layout.fillWidth: true
                    AppButton { text: "Retry search"; onClicked: library.searchEngine.retry() }
                }
                ListView {
                    id: cardList; objectName: "libraryCardList"; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: library.visibleCards; currentIndex: -1; keyNavigationEnabled: true; boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    onContentYChanged: if (ui.page === 1 && !ui.restoringDeck && !library.restoringSearchSelection) { ui.libraryScrollY = Math.max(0, contentY); librarySettings.cardScrollY = Math.max(0, contentY) }
                    Component.onCompleted: Qt.callLater(function() {
                        library.restoreScroll(ui.libraryScrollY)
                        library.hydrateCardSelection()
                    })
                    delegate: Item {
                        id: cardRow
                        required property var modelData
                        required property int index
                        readonly property int headingHeight: modelData.groupStart ? 44 : 0
                        width: cardList.width
                        height: headingHeight + 82
                        RowLayout {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            height: cardRow.headingHeight
                            visible: cardRow.headingHeight > 0
                            spacing: 12
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                Layout.minimumWidth: 16
                                height: 1
                                color: Theme.ledgerRule
                            }
                            Label {
                                textFormat: Text.PlainText
                                text: cardRow.modelData.searchGroup || ""
                                color: Theme.ink
                                font.family: Theme.monoFont
                                font.pixelSize: 16
                                font.weight: Font.DemiBold
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                                Layout.maximumWidth: Math.max(0, cardRow.width - 72)
                                ToolTip.visible: groupHover.hovered
                                ToolTip.text: "Search layer: results are ordered from exact matches through semantic matches."
                                HoverHandler { id: groupHover }
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                Layout.minimumWidth: 16
                                height: 1
                                color: Theme.ledgerRule
                            }
                        }
                        ItemDelegate {
                            id: cardButton
                            y: cardRow.headingHeight
                            width: cardRow.width
                            height: 82
                            highlighted: library.selectedCardIds.indexOf(cardRow.modelData.id) >= 0
                            CardActionsMenu {
                                id: cardActionsMenu
                                ui: library.ui
                                cardId: cardRow.modelData.id
                                onBeforeAction: library.chooseCard(cardRow.index)
                            }
                            MouseArea {
                                id: cardMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                onClicked: function(mouse) {
                                    cardList.forceActiveFocus()
                                    if (mouse.button === Qt.RightButton) {
                                        cardActionsMenu.cardId = cardRow.modelData.id
                                        cardActionsMenu.popup()
                                    } else {
                                        library.chooseCardWithModifiers(cardRow.index, mouse.modifiers)
                                    }
                                }
                                onDoubleClicked: function(mouse) {
                                    if (mouse.button === Qt.LeftButton)
                                        library.openCard(cardRow.modelData)
                                }
                            }
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
                            background: Rectangle {
                                color: cardButton.highlighted || cardMouse.containsMouse ? Theme.accentSoft : Theme.transparent
                                border.width: cardButton.highlighted ? 2 : (cardList.activeFocus && cardList.currentIndex === cardRow.index ? 1 : 0)
                                border.color: cardButton.highlighted ? Theme.accent : Theme.rule
                                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.rule }
                            }
                        }
                    }
                    Keys.onPressed: function(event) {
                        if (!visibleCards[currentIndex])
                            return
                        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                            if (event.modifiers & (Qt.ControlModifier | Qt.MetaModifier | Qt.ShiftModifier))
                                library.chooseCardWithModifiers(currentIndex, event.modifiers)
                            else
                                library.openCard(visibleCards[currentIndex])
                            event.accepted = true
                        } else if (event.key === Qt.Key_Space) {
                            library.chooseCardWithModifiers(currentIndex, event.modifiers)
                            event.accepted = true
                        } else if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
                            const next = event.key === Qt.Key_Up ? Math.max(0, currentIndex - 1) : Math.min(count - 1, currentIndex + 1)
                            currentIndex = next
                            ui.selectedCardId = visibleCards[next].id
                            if (!(event.modifiers & (Qt.ControlModifier | Qt.MetaModifier)))
                                library.chooseCardWithModifiers(next, event.modifiers)
                            event.accepted = true
                        }
                    }
                    Label { visible: cardList.count === 0 && (!library.searchEngine.busy || !library.searchingCards); anchors.centerIn: parent; text: ui.cardSearchText ? "No cards match this search." : "This deck has no cards yet."; color: Theme.inkMuted; wrapMode: Text.Wrap }
                }
            }
        }
        Item {
            visible: !library.effectiveLeftOpen && !library.cardsVisible
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
        Item {
            id: reopenCards
            visible: !library.cardsVisible && !!ui.selectedDeck.id
            Layout.preferredWidth: 56
            Layout.minimumWidth: 56
            Layout.maximumWidth: 56
            Layout.fillHeight: true
            Rectangle { anchors.fill: parent; color: Theme.surface; border.color: Theme.rule }
            PaneToggleButton {
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: 2
                collapsed: true
                rightSide: true
                hint: "Open card pane"
                onClicked: library.rightOpen = true
            }
        }
    }
}
