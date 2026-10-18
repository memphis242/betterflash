pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import "DeckSearch.js" as DeckSearch

Item {
    id: library
    required property var ui
    property string deckSearchText: librarySettings.deckQuery
    property string deckContentSearchText: librarySettings.deckContentQuery
    property bool searchReady: false
    property var searchCollapsedDecks: ({})
    readonly property bool searchingDeckContent: deckContentSearchText.trim().length > 0
    readonly property bool searchingDecks: DeckSearch.normalize(deckSearchText).length > 0 || searchingDeckContent
    readonly property var deckContentMatches: DeckSearch.aggregateContent(app.decks, searchingDeckContent ? searchEngine.deckResults : [])
    property var filterTags: readStoredTags(librarySettings.cardFilterTags)
    property string filterKind: librarySettings.cardFilterKind
    property string filterState: librarySettings.cardFilterState
    readonly property int activeFilterCount: filterTags.length + (filterKind ? 1 : 0) + (filterState ? 1 : 0)
    readonly property var scopedCards: app.cards.filter(card => !ui.selectedDeck.id || card.deckId === ui.selectedDeck.id)
    readonly property var availableTags: collectTags()
    property bool leftOpen: librarySettings.leftOpen
    property bool rightOpen: librarySettings.rightOpen
    readonly property bool cardsVisible: rightOpen
    property bool restoringTree: true
    readonly property bool effectiveLeftOpen: leftOpen && (!cardsVisible || width >= 720)
    property real splitRatio: librarySettings.splitRatio
    property var expandedDecks: ({})
    property bool expansionInitialized: false
    property bool deckSearchVisible: librarySettings.deckSearchVisible
    property bool deckContentSearchVisible: librarySettings.deckContentSearchVisible
    property bool cardSearchVisible: librarySettings.cardSearchVisible
    property bool weakestFirst: librarySettings.weakestFirst
    property bool restoringSearchSelection: false
    property string searchPageAnchorId: ""
    property bool resetSearchPage: false
    property int pageIndex: 0
    property string pageAnchorId: ""
    property bool pageChangePending: false
    readonly property var cardPages: paginateCards()
    readonly property int boundedPageIndex: Math.max(0, Math.min(pageIndex, cardPages.length - 1))
    readonly property var currentPage: cardPages.length > 0 ? cardPages[boundedPageIndex] : ({ start: 0, end: 0 })
    property var dragPageSnapshot: []
    readonly property var pageCards: dragging && dragKind === "cards" ? dragPageSnapshot : cardsOnPage()
    property bool dragging: false
    property string dragKind: ""
    property string dragDeckId: ""
    property var dragCardIds: []
    property point dragPosition: Qt.point(0, 0)
    property string dropDeckId: ""
    property string dropParentId: ""
    property string dropMode: ""
    property bool dropValid: false
    property string dropHint: ""
    property real dropLineY: 0
    property int dropDepth: 0
    property string hoverExpandId: ""
    property var selectedCardIds: []
    property string cardSelectionAnchorId: ""
    readonly property var searchEngine: librarySearch
    readonly property bool searchingCards: ui.cardSearchText.trim().length > 0
    readonly property var sourceCards: searchingCards ? (searchEngine.results || []) : scopedCards
    readonly property var visibleCards: filteredCards()
    readonly property var treeRows: flattenTree()
    ListModel { id: treeModel; dynamicRoles: true }
    onTreeRowsChanged: synchronizeTreeModel()

    Settings {
        id: librarySettings
        category: "libraryFolders"
        property bool leftOpen: true
        property bool rightOpen: false
        property real splitRatio: 0.5
        property string expanded: ""
        property real treeScrollY: 0
        property real cardScrollY: 0
        property string deckQuery: ""
        property string deckContentQuery: ""
        property bool deckSearchVisible: false
        property bool deckContentSearchVisible: false
        property bool cardSearchVisible: false
        property bool weakestFirst: false
        property string cardFilterTags: "[]"
        property string cardFilterKind: ""
        property string cardFilterState: ""
    }

    Component.onCompleted: {
        library.initializeExpansion()
        library.synchronizeTreeModel()
        library.searchReady = true
        library.updateCardSearch()
        library.updateDeckSearch()
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
    onRightOpenChanged: {
        librarySettings.rightOpen = rightOpen
        if (!rightOpen && cardFilters && cardFilters.visible) cardFilters.close()
    }
    onSplitRatioChanged: librarySettings.splitRatio = splitRatio
    onDeckSearchVisibleChanged: librarySettings.deckSearchVisible = deckSearchVisible
    onDeckContentSearchVisibleChanged: librarySettings.deckContentSearchVisible = deckContentSearchVisible
    onCardSearchVisibleChanged: librarySettings.cardSearchVisible = cardSearchVisible
    onWeakestFirstChanged: { librarySettings.weakestFirst = weakestFirst; resetPage() }
    onDeckSearchTextChanged: {
        librarySettings.deckQuery = deckSearchText
        searchCollapsedDecks = ({})
    }
    onDeckContentSearchTextChanged: {
        librarySettings.deckContentQuery = deckContentSearchText
        searchCollapsedDecks = ({})
        updateDeckSearch()
    }
    onFilterTagsChanged: { librarySettings.cardFilterTags = JSON.stringify(filterTags); filtersChanged() }
    onFilterKindChanged: { librarySettings.cardFilterKind = filterKind; filtersChanged() }
    onFilterStateChanged: { librarySettings.cardFilterState = filterState; filtersChanged() }

    function readStoredTags(value) {
        try {
            const tags = JSON.parse(value)
            return Array.isArray(tags) ? tags.filter(tag => typeof tag === "string" && tag.length > 0) : []
        } catch (error) { return [] }
    }
    function cardTags(card) {
        return String(card.tags || "").split(",").map(tag => tag.trim()).filter(tag => tag.length > 0)
    }
    function cardAssessment(card) {
        if (!card) return ({})
        if (card.weakness && typeof card.weakness === "object") return card.weakness
        const variants = card.variantWeaknesses
        if (!variants || typeof variants !== "object") return ({})
        const assessments = Object.keys(variants).map(key => variants[key]).filter(value => value && typeof value === "object")
        if (!assessments.length) return ({})
        const ranks = ({ unassessed: 0, steady: 1, watch: 2, fragile: 3, "needs-study": 4 })
        return assessments.sort((a, b) => (ranks[b.tier] || 0) - (ranks[a.tier] || 0) || Number(b.score || 0) - Number(a.score || 0))[0]
    }
    function weaknessRank(card) {
        const assessment = cardAssessment(card)
        const ranks = ({ unassessed: 0, steady: 1, watch: 2, fragile: 3, "needs-study": 4 })
        return (ranks[assessment.tier] || 0) * 1000 + Number(assessment.score || 0)
    }
    function collectTags() {
        const counts = new Map()
        for (const card of scopedCards)
            for (const tag of new Set(cardTags(card))) counts.set(tag, (counts.get(tag) || 0) + 1)
        for (const tag of filterTags)
            if (!counts.has(tag)) counts.set(tag, 0)
        return Array.from(counts.keys()).sort((a, b) => a.localeCompare(b)).map(tag => ({ tag: tag, count: counts.get(tag) }))
    }
    function matchesCardFilter(card) {
        if (filterTags.length > 0 && !cardTags(card).some(tag => filterTags.indexOf(tag) >= 0)) return false
        if (filterKind && card.kind !== filterKind) return false
        const reviews = Number(card.reviewCount || 0)
        const due = Number(card.dueCount || 0)
        switch (filterState) {
        case "new": return reviews === 0
        case "reviewed": return reviews > 0
        case "due": return due > 0
        case "later": return due === 0
        default: return true
        }
    }
    function filteredCards() {
        const groups = []
        const groupIndexes = ({})
        for (const card of sourceCards) {
            if (!matchesCardFilter(card)) continue
            const group = card.searchGroup || ""
            const groupKey = group || "\u0000normal"
            if (groupIndexes[groupKey] === undefined) { groupIndexes[groupKey] = groups.length; groups.push([]) }
            groups[groupIndexes[groupKey]].push(Object.assign({}, card, { groupStart: false }))
        }
        const result = []
        for (const group of groups) {
            if (weakestFirst) group.sort((a, b) => weaknessRank(b) - weaknessRank(a))
            for (let index = 0; index < group.length; ++index)
                result.push(Object.assign({}, group[index], { groupStart: !!group[index].searchGroup && (index === 0 || group[index].searchGroup !== group[index - 1].searchGroup) }))
        }
        return result
    }
    function filtersChanged() {
        if (!searchReady) return
        resetSearchPage = true
        searchPageAnchorId = ""
        resetPage()
        updateCardSearch()
        Qt.callLater(function () {
            const visibleIds = new Set(library.visibleCards.map(card => card.id))
            library.selectedCardIds = library.selectedCardIds.filter(id => visibleIds.has(id))
            if (!visibleIds.has(library.cardSelectionAnchorId)) library.cardSelectionAnchorId = ""
        })
    }
    function clearFilters() { filterTags = []; filterKind = ""; filterState = "" }
    function deckMatchLabel(row) {
        if (!row.actualMatch) return ""
        if (searchingDeckContent && row.contentMatch)
            return row.contentMatch.label + " / " + row.contentMatch.count
        return row.match ? row.match.label : ""
    }
    function deckMatchHint(row) {
        if (!row.actualMatch) return "Ancestor shown to preserve deck hierarchy"
        const nameHint = row.match ? "Deck name: " + row.match.label.toLowerCase() + ". " : ""
        if (!searchingDeckContent || !row.contentMatch) return nameHint
        const hit = row.contentMatch
        return nameHint + "Best card match: " + hit.label.toLowerCase() + ". " + hit.count
            + (hit.count === 1 ? " matching card" : " matching cards") + ": " + hit.ownCount
            + " in this deck, " + (hit.count - hit.ownCount) + " in subdecks."
    }

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
    function flattenTree() {
        const rows = []
        const query = DeckSearch.normalize(deckSearchText)
        const active = searchingDecks
        const content = deckContentMatches
        const children = ({})
        const matches = ({})
        const actual = ({})
        const best = ({})
        for (const deck of app.decks) {
            const parentId = deck.parentId || ""
            if (!children[parentId]) children[parentId] = []
            children[parentId].push(deck)
            matches[deck.id] = query ? DeckSearch.match(deck.name, query) : null
            actual[deck.id] = (!query || !!matches[deck.id]) && (!searchingDeckContent || !!content[deck.id])
        }
        function rank(deck, ancestors) {
            if (ancestors.indexOf(deck.id) >= 0) return Infinity
            const match = matches[deck.id]
            const hit = content[deck.id]
            const scoreRank = hit ? (1 - Math.max(-1, Math.min(1, hit.score))) / 4 : 0
            let result = actual[deck.id] ? (hit ? hit.tier * 10000 : 0)
                + (match ? match.tier * 100 + match.distance : 0) + scoreRank : Infinity
            for (const child of children[deck.id] || [])
                result = Math.min(result, rank(child, ancestors.concat([deck.id])))
            best[deck.id] = result
            return result
        }
        for (const root of children[""] || []) rank(root, [])
        function ordered(id) {
            return (children[id] || []).slice().sort((a, b) => {
                if (active && best[a.id] !== best[b.id]) return best[a.id] < best[b.id] ? -1 : 1
                return String(a.name).localeCompare(String(b.name))
            })
        }
        function append(deck, depth, ancestors) {
            if (ancestors.indexOf(deck.id) >= 0 || active && best[deck.id] === Infinity) return
            const descendants = ordered(deck.id).filter(child => !active || best[child.id] !== Infinity)
            const expanded = active ? !library.searchCollapsedDecks[deck.id] : !!library.expandedDecks[deck.id]
            rows.push({ deck: deck, id: deck.id, depth: depth, expanded: expanded,
                hasChildren: descendants.length > 0, match: matches[deck.id],
                actualMatch: actual[deck.id], contentMatch: content[deck.id] || null })
            if (expanded)
                for (const child of descendants) append(child, depth + 1, ancestors.concat([deck.id]))
        }
        for (const root of ordered("")) append(root, 0, [])
        return rows
    }
    function synchronizeTreeModel() {
        if (!treeModel || !treeList) return
        const focusedId = treeList.currentIndex >= 0 && treeList.currentIndex < treeModel.count
            ? treeModel.get(treeList.currentIndex).entry.id : ""
        for (let index = 0; index < treeRows.length; ++index) {
            const row = treeRows[index]
            let found = index
            while (found < treeModel.count && treeModel.get(found).entry.id !== row.id) ++found
            if (found === treeModel.count) treeModel.insert(index, { entry: row })
            else {
                if (found !== index) treeModel.move(found, index, 1)
                treeModel.setProperty(index, "entry", row)
            }
        }
        if (treeModel.count > treeRows.length) treeModel.remove(treeRows.length, treeModel.count - treeRows.length)
        treeList.currentIndex = treeRows.findIndex(row => row.id === focusedId)
    }
    function expandAllDecks(expanded) {
        if (searchingDecks) {
            const collapsed = ({})
            if (!expanded) for (const deck of app.decks) collapsed[deck.id] = true
            searchCollapsedDecks = collapsed
            return
        }
        const state = ({})
        if (expanded)
            for (const deck of app.decks) state[deck.id] = true
        expandedDecks = state
    }
    function clearDeckSelection() {
        ui.setDeck("", true)
        ui.selectedCardId = ""
        selectedCardIds = []
        cardSelectionAnchorId = ""
        treeList.currentIndex = -1
        rightOpen = true
        resetPage()
        updateCardSearch()
    }
    function paginateCards() {
        const pages = []
        const cards = visibleCards
        const available = Math.max(82, Math.floor(cardList.height))
        let start = 0
        while (start < cards.length) {
            let end = start
            let used = 0
            while (end < cards.length) {
                const heading = cards[end].searchGroup && (end === start || cards[end].groupStart) ? 44 : 0
                const cost = 82 + heading
                if (end > start && used + cost > available) break
                used += cost
                ++end
            }
            pages.push({ start: start, end: end })
            start = end
        }
        return pages
    }
    function cardsOnPage() {
        const result = []
        for (let index = currentPage.start; index < currentPage.end; ++index)
            result.push(Object.assign({}, visibleCards[index], { libraryIndex: index,
                groupStart: !!visibleCards[index].searchGroup && (index === currentPage.start || visibleCards[index].groupStart) }))
        return result
    }
    function pageForCard(id) {
        const index = visibleCards.findIndex(card => card.id === id)
        return index < 0 ? -1 : cardPages.findIndex(page => index >= page.start && index < page.end)
    }
    function rememberPage() {
        pageAnchorId = visibleCards[currentPage.start] ? visibleCards[currentPage.start].id : ""
    }
    function resetPage() {
        pageIndex = 0
        pageAnchorId = ""
        Qt.callLater(function () { library.rememberPage(); library.syncCurrentCard() })
    }
    function syncCurrentCard() {
        cardList.currentIndex = pageCards.findIndex(card => card.id === ui.selectedCardId)
        cardList.contentY = 0
    }
    function changePage(next) {
        pageIndex = Math.max(0, Math.min(next, cardPages.length - 1))
        rememberPage()
        syncCurrentCard()
    }
    function preservePage() {
        if (pageChangePending) return
        pageChangePending = true
        Qt.callLater(function () {
            const page = library.pageForCard(library.pageAnchorId)
            library.pageIndex = page >= 0 ? page : library.boundedPageIndex
            library.rememberPage()
            library.syncCurrentCard()
            library.pageChangePending = false
        })
    }
    onCardPagesChanged: preservePage()
    function toggleDeck(id) {
        if (searchingDecks) {
            const collapsed = Object.assign({}, searchCollapsedDecks)
            collapsed[id] = !collapsed[id]
            searchCollapsedDecks = collapsed
            return
        }
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
        ui.setDeck(id || "", true)
        if (id) revealDeck(id)
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
        const nextPage = pageForCard(card.id)
        if (nextPage >= 0 && nextPage !== boundedPageIndex) changePage(nextPage)
        cardList.currentIndex = index - currentPage.start
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
        const nextPage = pageForCard(card.id)
        if (nextPage >= 0 && nextPage !== boundedPageIndex) changePage(nextPage)
        cardList.currentIndex = index - currentPage.start
        ui.selectedCardId = card.id
    }
    function openCard(card) {
        if (!card)
            return
        ui.selectedCardId = card.id
        ui.openSelectedDetail()
    }
    function restoreScroll(value) {
        const page = pageForCard(ui.selectedCardId)
        if (page >= 0) changePage(page)
        else syncCurrentCard()
    }
    function focusCards() {
        if (!rightOpen)
            return
        cardList.forceActiveFocus()
        if (cardList.currentIndex < 0 && cardList.count > 0)
            cardList.currentIndex = Math.max(0, pageCards.findIndex(card => card.id === ui.selectedCardId))
    }
    function focusDeckSearch() {
        leftOpen = true
        if (width < 720) rightOpen = false
        deckSearchVisible = true
        deckSearch.forceActiveFocus()
        deckSearch.selectAll()
    }

    function toggleDeckSearch() {
        if (!deckSearchVisible) focusDeckSearch()
        else { deckSearchVisible = false; treeList.forceActiveFocus() }
    }
    function toggleCardSearch() {
        cardSearchVisible = !cardSearchVisible
        if (cardSearchVisible) cardSearch.forceActiveFocus()
        else focusCards()
    }
    function updateDeckSearch() {
        if (!searchReady) return
        searchEngine.setDeckSearchActive(ui.page === 1)
        searchEngine.setDeckSearch(deckContentSearchText)
    }
    function updateCardSearch() {
        if (!searchReady) return
        searchEngine.setActive(ui.page === 1)
        const ids = activeFilterCount > 0 ? app.cards.filter(card => matchesCardFilter(card)).map(card => card.id) : []
        searchEngine.setCardFilter(ids, activeFilterCount > 0)
        searchEngine.setSearch(ui.selectedDeck.id || "", ui.cardSearchText)
    }

    function selectCardAfterSearch() {
        if (resetSearchPage) {
            pageIndex = 0
            pageAnchorId = ""
            searchPageAnchorId = ""
            resetSearchPage = false
        } else if (searchPageAnchorId) pageAnchorId = searchPageAnchorId
        preservePage()
        library.restoringSearchSelection = false
    }

    function beginDrag(kind, id, position) {
        if (app.busy) return false
        dragKind = kind
        dragDeckId = kind === "deck" ? id : ""
        if (kind === "deck") {
            // ListView retains its current item outside the visible viewport during a drag.
            treeList.currentIndex = treeRows.findIndex(row => row.id === id)
            treeList.forceActiveFocus()
        }
        if (kind === "cards") {
            dragPageSnapshot = pageCards
            if (selectedCardIds.indexOf(id) < 0)
                chooseCardWithModifiers(visibleCards.findIndex(card => card.id === id), Qt.NoModifier)
            dragCardIds = selectedCardIds.slice()
            chooseCard(visibleCards.findIndex(card => card.id === id))
            cardList.forceActiveFocus()
        } else dragCardIds = []
        dragging = true
        updateDrag(position)
        return true
    }
    function deckIsWithin(id, ancestorId) {
        const visited = []
        let current = ui.deckById(id)
        while (current && current.id && visited.indexOf(current.id) < 0) {
            if (current.id === ancestorId) return true
            visited.push(current.id)
            current = ui.deckById(current.parentId || "")
        }
        return false
    }
    function insertionLine(parentId) {
        const moving = ui.deckById(dragDeckId)
        const siblings = childrenOf(parentId).filter(deck => deck.id !== dragDeckId)
        const after = siblings.find(deck => String(deck.name).localeCompare(String(moving.name)) > 0)
        if (after) {
            const row = treeRows.findIndex(item => item.id === after.id)
            if (row >= 0) return row * 50
        }
        if (parentId) {
            const parent = treeRows.findIndex(item => item.id === parentId)
            if (parent >= 0) {
                let end = parent + 1
                while (end < treeRows.length && treeRows[end].depth > treeRows[parent].depth) ++end
                return end * 50
            }
        }
        return treeRows.length * 50
    }
    function updateDrag(position) {
        dragPosition = position
        dropValid = false
        dropDeckId = ""
        dropMode = ""
        dropHint = "Drag onto a deck"
        if (!dragging || !effectiveLeftOpen) { hoverExpandId = ""; return }
        const point = library.mapToItem(treeList, position.x, position.y)
        if (point.x < 0 || point.x > treeList.width || point.y < 0 || point.y > treeList.height) {
            hoverExpandId = ""
            return
        }
        const contentY = point.y + treeList.contentY
        const index = treeList.indexAt(point.x, contentY)
        const row = treeRows[index]
        if (dragKind === "cards") {
            hoverExpandId = row && row.hasChildren && !row.expanded ? row.id : ""
            if (!row) { dropHint = "Choose a destination deck"; return }
            dropDeckId = row.id
            dropParentId = row.id
            dropMode = "cards"
            dropValid = true
            dropHint = "Move " + dragCardIds.length + (dragCardIds.length === 1 ? " card into " : " cards into ") + row.deck.name
            return
        }
        const localY = row ? contentY - index * 50 : 0
        const inside = !!row && localY >= 12 && localY <= 38
        dropDeckId = row ? row.id : ""
        dropParentId = row ? (inside ? row.id : row.deck.parentId || "") : ""
        dropMode = inside ? "child" : "sibling"
        dropDepth = row ? (inside ? row.depth + 1 : row.depth) : 0
        const invalid = (!!row && row.id === dragDeckId) || deckIsWithin(dropParentId, dragDeckId)
        const sameParent = (ui.deckById(dragDeckId).parentId || "") === dropParentId
        dropValid = !invalid && !sameParent
        dropHint = invalid ? "A deck cannot contain itself or its ancestor"
            : sameParent ? "Already at this level (alphabetical order)"
            : inside ? "Inside " + row.deck.name + " (A-Z)"
            : row ? "Alongside " + row.deck.name + " (A-Z)" : "Top-level deck (A-Z)"
        hoverExpandId = inside && !invalid && row.hasChildren && !row.expanded ? row.id : ""
        dropLineY = insertionLine(dropParentId)
    }
    function finishDrag() {
        if (!dragging) return
        const valid = dropValid
        const kind = dragKind
        const deckId = dragDeckId
        const parentId = dropParentId
        const ids = dragCardIds.slice()
        cancelDrag()
        if (!valid) return
        if (kind === "deck") app.moveDeck(deckId, parentId)
        else app.moveCards(ids, parentId)
        if (parentId) {
            const state = Object.assign({}, expandedDecks)
            state[parentId] = true
            expandedDecks = state
        }
    }
    function cancelDrag() {
        dragging = false
        dropValid = false
        dropMode = ""
        dropHint = ""
        hoverExpandId = ""
    }
    onHoverExpandIdChanged: hoverTimer.restart()
    Timer {
        id: hoverTimer
        interval: 650
        onTriggered: {
            const row = library.treeRows.find(item => item.id === library.hoverExpandId)
            if (library.dragging && row && !row.expanded) library.toggleDeck(row.id)
            if (library.dragging) library.updateDrag(library.dragPosition)
        }
    }
    Timer {
        interval: 30
        repeat: true
        running: library.dragging && library.effectiveLeftOpen
        onTriggered: {
            const point = library.mapToItem(treeList, library.dragPosition.x, library.dragPosition.y)
            if (point.x < 0 || point.x > treeList.width) return
            const speed = point.y < 36 ? -12 : point.y > treeList.height - 36 ? 12 : 0
            if (speed !== 0) {
                treeList.contentY = Math.max(0, Math.min(Math.max(0, treeList.contentHeight - treeList.height), treeList.contentY + speed))
                library.updateDrag(library.dragPosition)
            }
        }
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
            library.resetSearchPage = true
            library.searchPageAnchorId = ""
            library.resetPage()
            if (ui.selectedDeck.id) {
                library.revealDeck(ui.selectedDeck.id)
                Qt.callLater(library.hydrateCardSelection)
            }
        }
        function onCardsChanged() {
            library.updateCardSearch()
            const valid = new Set(app.cards.filter(card => !ui.selectedDeck.id || card.deckId === ui.selectedDeck.id).map(card => card.id))
            library.selectedCardIds = library.selectedCardIds.filter(id => valid.has(id))
            if (library.cardSelectionAnchorId && !valid.has(library.cardSelectionAnchorId))
                library.cardSelectionAnchorId = ""
        }
    }

    Connections {
        target: library.searchEngine
        function onResultsAboutToChange() {
            if (!library.restoringSearchSelection) {
                library.searchPageAnchorId = library.resetSearchPage ? "" : library.pageAnchorId
            }
            library.restoringSearchSelection = true
        }
        function onResultsChanged() { Qt.callLater(library.selectCardAfterSearch) }
    }

    onVisibleChanged: {
        updateCardSearch()
        updateDeckSearch()
        if (!visible && cardFilters && cardFilters.visible) cardFilters.close()
    }
    Connections {
        target: ui
        function onSelectedDeckChanged() { library.updateCardSearch() }
        function onPageChanged() { library.updateCardSearch(); library.updateDeckSearch() }
        function onCardSearchTextChanged() { library.resetSearchPage = true; library.searchPageAnchorId = ""; library.resetPage(); library.updateCardSearch() }
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
                    spacing: 8
                    Label { Layout.minimumWidth: 0; elide: Text.ElideRight; text: "Decks"; color: Theme.ink; font.family: Theme.uiFont; font.pixelSize: 24; font.weight: Font.DemiBold }
                    GlyphButton { glyph: "expand-tree"; hint: "Expand all decks"; enabled: app.decks.length > 0; onClicked: library.expandAllDecks(true) }
                    GlyphButton { glyph: "collapse-tree"; hint: "Collapse all decks"; enabled: app.decks.length > 0; onClicked: library.expandAllDecks(false) }
                    Item { Layout.fillWidth: true }
                    PaneToggleButton { hint: "Collapse deck pane"; onClicked: library.leftOpen = false }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    ToolButton {
                        id: addDeckButton
                        implicitWidth: 40; implicitHeight: 40; padding: 0
                        enabled: !app.busy
                        ToolTip.visible: hovered || activeFocus
                        ToolTip.text: "Add a top-level deck"
                        Accessible.name: ToolTip.text
                        contentItem: Item { Icon { kind: "plus"; width: 21; height: 21; anchors.centerIn: parent; stroke: Theme.onAccent; lineWidth: 1.8 } }
                        background: Rectangle { radius: width / 2; color: Theme.accent; border.width: addDeckButton.activeFocus || addDeckButton.hovered ? 2 : 0; border.color: Theme.ink }
                        onClicked: ui.openDeckEditor("")
                    }
                    GlyphButton { glyph: "subdeck-add"; hint: "Add a subdeck to the selected deck"; enabled: !!ui.selectedDeck.id && !app.busy; onClicked: ui.openDeckEditor("", ui.selectedDeck.id) }
                    Item { Layout.fillWidth: true }
                    GlyphButton {
                        glyph: "search"; selected: library.searchingDecks || library.deckSearchVisible
                        hint: (library.deckSearchVisible ? "Hide deck search" : "Show deck search") + " (" + shortcuts.bindings.deckSearch + ")"
                        onClicked: library.toggleDeckSearch()
                    }
                    GlyphButton {
                        glyph: "more"; hint: "Deck actions"; onClicked: deckMenu.open()
                        DeckActionsMenu { id: deckMenu; ui: library.ui; deckId: library.ui.selectedDeck.id || ""; headerMode: true }
                    }
                }
                ColumnLayout {
                    visible: library.deckSearchVisible
                    Layout.fillWidth: true
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        TextField {
                            id: deckSearch; objectName: "deckSearchField"
                            Layout.fillWidth: true; Layout.minimumWidth: 0
                            placeholderText: "Find a deck"; maximumLength: 128; text: library.deckSearchText
                            Accessible.name: "Find a deck by name"
                            onTextEdited: library.deckSearchText = text
                            Keys.onEscapePressed: { library.deckSearchVisible = false; treeList.forceActiveFocus() }
                            Keys.onReturnPressed: treeList.forceActiveFocus()
                        }
                        GlyphButton { glyph: "close"; hint: "Clear deck name search"; visible: library.deckSearchText.length > 0; onClicked: { library.deckSearchText = ""; deckSearch.forceActiveFocus() } }
                        GlyphButton {
                            glyph: library.deckContentSearchVisible ? "up" : "down"
                            selected: library.searchingDeckContent || library.deckContentSearchVisible
                            hint: library.deckContentSearchVisible ? "Hide search within decks" : "Search within decks"
                            onClicked: {
                                library.deckContentSearchVisible = !library.deckContentSearchVisible
                                if (library.deckContentSearchVisible) deckContentSearch.forceActiveFocus()
                                else deckSearch.forceActiveFocus()
                            }
                        }
                    }
                    RowLayout {
                        visible: library.deckContentSearchVisible
                        Layout.fillWidth: true
                        spacing: 6
                        TextField {
                            id: deckContentSearch; objectName: "deckContentSearchField"
                            Layout.fillWidth: true; Layout.minimumWidth: 0
                            placeholderText: "Search within decks"; maximumLength: 256; text: library.deckContentSearchText
                            Accessible.name: "Find decks by card content, tag, or meaning"
                            onTextEdited: library.deckContentSearchText = text
                            Keys.onEscapePressed: { library.deckContentSearchVisible = false; deckSearch.forceActiveFocus() }
                            Keys.onReturnPressed: treeList.forceActiveFocus()
                            ToolTip.visible: hovered
                            ToolTip.text: "Find cards by content, tag, or meaning within decks and subdecks. Combined with the deck name search."
                        }
                        GlyphButton { glyph: "close"; hint: "Clear search within decks"; visible: library.deckContentSearchText.length > 0; onClicked: { library.deckContentSearchText = ""; deckContentSearch.forceActiveFocus() } }
                    }
                }
                RowLayout {
                    visible: library.searchingDeckContent && (library.searchEngine.deckBusy || library.searchEngine.deckStatus.length > 0)
                    Layout.fillWidth: true; spacing: 8
                    BusyIndicator { running: library.searchEngine.deckBusy; visible: running; implicitWidth: 18; implicitHeight: 18 }
                    Label {
                        Layout.fillWidth: true; textFormat: Text.PlainText
                        text: library.searchEngine.deckStatus; color: Theme.inkMuted
                        font.family: Theme.monoFont; font.pixelSize: 11; elide: Text.ElideRight
                        ToolTip.visible: deckStatusHover.hovered; ToolTip.text: text
                        HoverHandler { id: deckStatusHover }
                    }
                }
                ErrorBlock {
                    visible: library.searchingDeckContent && library.searchEngine.deckError.length > 0
                    message: library.searchEngine.deckError
                    Layout.fillWidth: true
                    AppButton { text: "Retry search"; onClicked: library.searchEngine.retryDeckSearch() }
                }
                Label {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 18
                    Layout.minimumHeight: 18
                    Layout.maximumHeight: 18
                    text: library.dragging ? library.dropHint : ""
                    textFormat: Text.PlainText
                    color: library.dropValid ? Theme.accent : Theme.inkMuted
                    font.family: Theme.uiFont
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    ToolTip.visible: dropHintHover.hovered
                    ToolTip.text: text
                    HoverHandler { id: dropHintHover }
                }
                ListView {
                    id: treeList; objectName: "libraryDeckTree"; Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                    model: treeModel; keyNavigationEnabled: true; boundsBehavior: Flickable.StopAtBounds
                    interactive: !library.dragging
                    MouseArea {
                        parent: treeList
                        anchors.fill: parent
                        z: -1
                        acceptedButtons: Qt.LeftButton
                        onClicked: library.clearDeckSelection()
                    }
                    Rectangle {
                        parent: treeList
                        visible: library.dragging && library.dragKind === "deck" && library.dropMode.length > 0
                            && library.dropLineY >= treeList.contentY && library.dropLineY <= treeList.contentY + treeList.height
                        z: 10
                        x: Math.min(treeList.width - 20, 8 + library.dropDepth * 20)
                        y: Math.max(1, Math.min(treeList.height - 3, library.dropLineY - treeList.contentY - 1))
                        width: Math.max(12, treeList.width - x - 8)
                        height: 3
                        color: library.dropValid ? Theme.accent : Theme.inkMuted
                        Rectangle { width: 7; height: 7; radius: 3.5; anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; color: parent.color }
                    }
                    ScrollBar.vertical: ScrollBar {}
                    onContentYChanged: if (!library.restoringTree && library.visible && library.effectiveLeftOpen) librarySettings.treeScrollY = Math.max(0, contentY)
                    delegate: Item {
                        id: treeRow; required property var entry; required property int index
                        readonly property var modelData: entry
                        width: treeList.width; height: 50
                        function openContextMenu() { deckContextMenu.popup(treeRow, 0, height) }
                        Rectangle {
                            anchors.fill: parent; anchors.leftMargin: 4 + treeRow.modelData.depth * 20; anchors.rightMargin: 4; radius: 5
                            color: ui.selectedDeck.id === treeRow.modelData.id ? Theme.accentSoft : (treeMouse.containsMouse ? Theme.surfaceRaised : Theme.transparent)
                            border.width: library.dragging && library.dropDeckId === treeRow.modelData.id || ui.selectedDeck.id === treeRow.modelData.id || treeList.activeFocus && treeList.currentIndex === treeRow.index ? 2 : 0
                            border.color: library.dragging && library.dropDeckId === treeRow.modelData.id ? (library.dropValid ? Theme.accent : Theme.inkMuted) : ui.selectedDeck.id === treeRow.modelData.id ? Theme.accent : Theme.rule
                        }
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: 10 + treeRow.modelData.depth * 20; anchors.rightMargin: 12; spacing: 9
                            GlyphButton {
                                visible: treeRow.modelData.hasChildren; glyph: treeRow.modelData.expanded ? "down" : "chevron-right"; glyphSize: 15; implicitWidth: 26; implicitHeight: 30
                                hint: treeRow.modelData.expanded ? "Collapse deck" : "Expand deck"; onClicked: library.toggleDeck(treeRow.modelData.id)
                            }
                            Item { visible: !treeRow.modelData.hasChildren; implicitWidth: 26; implicitHeight: 30 }
                            Icon { kind: treeRow.modelData.expanded ? "folder-open" : "folder"; width: 22; height: 22; stroke: ui.selectedDeck.id === treeRow.modelData.id ? Theme.accent : Theme.inkMuted }
                            Label { Layout.fillWidth: true; textFormat: Text.PlainText; text: treeRow.modelData.deck.name; color: library.searchingDecks && !treeRow.modelData.actualMatch ? Theme.inkMuted : Theme.ink; font.family: Theme.contentFont; font.pixelSize: 17; elide: Text.ElideRight }
                            Label {
                                visible: library.searchingDecks && treeRow.modelData.actualMatch
                                text: library.deckMatchLabel(treeRow.modelData)
                                Layout.maximumWidth: Math.max(70, treeList.width * 0.45)
                                elide: Text.ElideRight
                                color: Theme.inkMuted; font.family: Theme.monoFont; font.pixelSize: 11
                                ToolTip.visible: matchHover.hovered
                                ToolTip.text: library.deckMatchHint(treeRow.modelData)
                                HoverHandler { id: matchHover }
                            }
                            Label { visible: !library.searchingDecks && Number(treeRow.modelData.deck.ownCardCount || 0) > 0; text: String(treeRow.modelData.deck.ownCardCount || 0); color: Theme.inkMuted; font.family: Theme.monoFont; font.pixelSize: 11 }
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
                            property point pressPosition: Qt.point(0, 0)
                            property bool dragged: false
                            preventStealing: true
                            cursorShape: library.dragging ? (library.dropValid ? Qt.DragMoveCursor : Qt.ForbiddenCursor) : Qt.ArrowCursor
                            onPressed: function(mouse) { pressPosition = mapToItem(library, mouse.x, mouse.y); dragged = false }
                            onPositionChanged: function(mouse) {
                                if (!(pressedButtons & Qt.LeftButton)) return
                                const position = mapToItem(library, mouse.x, mouse.y)
                                if (!dragged && Math.hypot(position.x - pressPosition.x, position.y - pressPosition.y) >= Qt.styleHints.startDragDistance) {
                                    dragged = library.beginDrag("deck", treeRow.modelData.id, position)
                                }
                                if (library.dragging) library.updateDrag(position)
                            }
                            onReleased: function(mouse) { if (dragged) library.finishDrag() }
                            onCanceled: library.cancelDrag()
                            onClicked: function(mouse) {
                                if (dragged) return
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
                        parent: treeList
                        visible: treeList.count === 0
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.verticalCenter: parent.verticalCenter
                        text: library.searchingDeckContent && library.searchEngine.deckBusy ? "Searching cards in decks..." : library.searchingDecks ? "No decks match this search." : "No decks yet"
                        color: Theme.inkMuted
                    }
                    AppButton {
                        parent: treeList
                        visible: treeList.count === 0 && !library.searchingDecks
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.verticalCenter
                        anchors.topMargin: 22
                        text: "New deck"
                        primary: true
                        onClicked: ui.openDeckEditor("")
                    }
                    Keys.onPressed: function(event) {
                        if (event.key === Qt.Key_Escape) {
                            if (library.dragging) library.cancelDrag()
                            else library.clearDeckSelection()
                            event.accepted = true
                            return
                        }
                        const row = library.treeRows[currentIndex]
                        if (!row) return
                        if (event.key === Qt.Key_Menu || event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier)) {
                            if (currentItem) currentItem.openContextMenu()
                            event.accepted = true
                        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) { library.chooseTreeRow(currentIndex); event.accepted = true }
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
                        Label { textFormat: Text.PlainText; text: (ui.selectedDeck.id ? ui.selectedDeck.name : "All decks") + " / " + library.visibleCards.length + " cards"; color: Theme.inkMuted; font.family: Theme.monoFont; font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true }
                    }
                    PaneToggleButton { rightSide: true; hint: "Close card pane"; onClicked: library.rightOpen = false }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    GlyphButton { glyph: "chevron-left-bold"; hint: "Previous page (Page Up)"; implicitWidth: 32; implicitHeight: 32; glyphSize: 15; enabled: library.boundedPageIndex > 0; onClicked: library.changePage(library.boundedPageIndex - 1) }
                    Label {
                        text: library.cardPages.length > 0 ? (library.boundedPageIndex + 1) + " / " + library.cardPages.length : "0 / 0"
                        color: Theme.ink; font.family: Theme.monoFont; font.pixelSize: 12
                        ToolTip.visible: pageHover.hovered
                        ToolTip.text: "Page " + (library.cardPages.length > 0 ? library.boundedPageIndex + 1 : 0) + " of " + library.cardPages.length + ". Page size fits the available height."
                        HoverHandler { id: pageHover }
                    }
                    GlyphButton { glyph: "chevron-right-bold"; hint: "Next page (Page Down)"; implicitWidth: 32; implicitHeight: 32; glyphSize: 15; enabled: library.boundedPageIndex + 1 < library.cardPages.length; onClicked: library.changePage(library.boundedPageIndex + 1) }
                    Item { Layout.fillWidth: true }
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
                    GlyphButton { glyph: "search"; selected: library.searchingCards || library.cardSearchVisible; hint: library.cardSearchVisible ? "Hide card search" : "Show card search"; onClicked: library.toggleCardSearch() }
                    GlyphButton {
                        id: cardFilterButton
                        glyph: "filter"; selected: library.activeFilterCount > 0 || cardFilters.visible
                        hint: "Filter cards" + (library.activeFilterCount > 0 ? " (" + library.activeFilterCount + " active)" : "")
                        onClicked: cardFilters.visible ? cardFilters.close() : cardFilters.open()
                    }
                    AppButton {
                        text: "Weakest first"
                        glyph: "review-ledger"
                        fill: library.weakestFirst ? Theme.accentSoft : Theme.transparent
                        outline: library.weakestFirst ? Theme.accent : Theme.rule
                        foreground: library.weakestFirst ? Theme.accent : Theme.ink
                        hint: library.weakestFirst ? "Return cards to their normal order" : "Sort cards by weakness within each search group"
                        onClicked: library.weakestFirst = !library.weakestFirst
                    }
                }
                RowLayout {
                    Layout.fillWidth: true; visible: library.cardSearchVisible; spacing: 6
                    TextField {
                        id: cardSearch; objectName: "libraryCardSearch"; Layout.fillWidth: true; Layout.minimumWidth: 0
                        placeholderText: "Find a card by content, tag, or meaning"; maximumLength: 256; text: ui.cardSearchText; onTextEdited: ui.cardSearchText = text
                        Accessible.name: "Find a card by content, tag, or meaning"
                        Keys.onEscapePressed: { library.cardSearchVisible = false; library.focusCards() }
                        Keys.onReturnPressed: library.focusCards()
                    }
                    GlyphButton { glyph: "close"; hint: "Clear card search"; visible: ui.cardSearchText.length > 0; onClicked: { ui.cardSearchText = ""; cardSearch.forceActiveFocus() } }
                }
                RowLayout {
                    visible: library.activeFilterCount > 0
                    Layout.fillWidth: true; spacing: 6
                    Label {
                        Layout.fillWidth: true
                        text: library.searchingCards ? library.visibleCards.length + " search results match filters"
                            : library.visibleCards.length + " of " + library.sourceCards.length + " cards match filters"
                        color: Theme.inkMuted; font.family: Theme.monoFont; font.pixelSize: 11; elide: Text.ElideRight
                    }
                    GlyphButton { glyph: "close"; hint: "Clear all card filters"; implicitWidth: 28; implicitHeight: 28; glyphSize: 13; onClicked: library.clearFilters() }
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
                    id: cardList; objectName: "libraryCardList"; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; model: library.pageCards; currentIndex: -1; keyNavigationEnabled: false; boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    interactive: !library.dragging && contentHeight > height
                    Component.onCompleted: Qt.callLater(function() {
                        library.restoreScroll(ui.libraryScrollY)
                        library.hydrateCardSelection()
                    })
                    delegate: Item {
                        id: cardRow
                        required property var modelData
                        required property int index
                        readonly property int libraryIndex: modelData.libraryIndex
                        readonly property int headingHeight: modelData.groupStart ? 44 : 0
                        width: cardList.width
                        height: headingHeight + 82
                        function openContextMenu() { cardActionsMenu.popup(cardRow, 0, height) }
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
                                onBeforeAction: library.chooseCard(cardRow.libraryIndex)
                            }
                            MouseArea {
                                id: cardMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                property point pressPosition: Qt.point(0, 0)
                                property bool dragged: false
                                preventStealing: true
                                cursorShape: library.dragging ? (library.dropValid ? Qt.DragMoveCursor : Qt.ForbiddenCursor) : Qt.ArrowCursor
                                onPressed: function(mouse) { pressPosition = mapToItem(library, mouse.x, mouse.y); dragged = false }
                                onPositionChanged: function(mouse) {
                                    if (!(pressedButtons & Qt.LeftButton)) return
                                    const position = mapToItem(library, mouse.x, mouse.y)
                                    if (!dragged && Math.hypot(position.x - pressPosition.x, position.y - pressPosition.y) >= Qt.styleHints.startDragDistance) {
                                        dragged = library.beginDrag("cards", cardRow.modelData.id, position)
                                    }
                                    if (library.dragging) library.updateDrag(position)
                                }
                                onReleased: function(mouse) { if (dragged) library.finishDrag() }
                                onCanceled: library.cancelDrag()
                                onClicked: function(mouse) {
                                    if (dragged) return
                                    cardList.forceActiveFocus()
                                    if (mouse.button === Qt.RightButton) {
                                        cardActionsMenu.cardId = cardRow.modelData.id
                                        cardActionsMenu.popup()
                                    } else {
                                        library.chooseCardWithModifiers(cardRow.libraryIndex, mouse.modifiers)
                                    }
                                }
                                onDoubleClicked: function(mouse) {
                                    if (!dragged && mouse.button === Qt.LeftButton)
                                        library.openCard(cardRow.modelData)
                                }
                            }
                            contentItem: ColumnLayout {
                                spacing: 5
                                RowLayout { Layout.fillWidth: true; spacing: 8
                                    Label { textFormat: Text.PlainText; text: ui.compactText(cardRow.modelData.front); color: Theme.ink; font.family: Theme.contentFont; font.pixelSize: 16; elide: Text.ElideRight; Layout.fillWidth: true }
                                    WeaknessBadge { assessment: library.cardAssessment(cardRow.modelData); compact: true; visible: !!assessment.assessed && (assessment.tier === "watch" || assessment.tier === "fragile" || assessment.tier === "needs-study") }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label {
                                        visible: !ui.selectedDeck.id
                                        textFormat: Text.PlainText
                                        text: ui.deckById(cardRow.modelData.deckId).name || ""
                                        color: Theme.inkMuted; font.family: Theme.monoFont; font.pixelSize: 11
                                        elide: Text.ElideRight; Layout.maximumWidth: cardRow.width * 0.45
                                        ToolTip.visible: deckNameHover.hovered
                                        ToolTip.text: text
                                        HoverHandler { id: deckNameHover }
                                    }
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
                        if (event.key === Qt.Key_PageUp || event.key === Qt.Key_PageDown) {
                            library.changePage(library.boundedPageIndex + (event.key === Qt.Key_PageUp ? -1 : 1))
                            if (cardList.count > 0) cardList.currentIndex = 0
                            event.accepted = true
                            return
                        }
                        if (event.key === Qt.Key_Escape && library.dragging) {
                            library.cancelDrag(); event.accepted = true; return
                        }
                        const card = library.pageCards[currentIndex]
                        if (!card) {
                            if ((event.key === Qt.Key_Down || event.key === Qt.Key_Up) && count > 0) {
                                library.chooseCardWithModifiers(library.currentPage.start, event.modifiers)
                                event.accepted = true
                            }
                            return
                        }
                        if (event.key === Qt.Key_Menu || event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier)) {
                            if (currentItem) currentItem.openContextMenu()
                            event.accepted = true
                        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                            if (event.modifiers & (Qt.ControlModifier | Qt.MetaModifier | Qt.ShiftModifier))
                                library.chooseCardWithModifiers(card.libraryIndex, event.modifiers)
                            else library.openCard(card)
                            event.accepted = true
                        } else if (event.key === Qt.Key_Space) {
                            library.chooseCardWithModifiers(card.libraryIndex, event.modifiers)
                            event.accepted = true
                        } else if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
                            const next = Math.max(0, Math.min(library.visibleCards.length - 1, card.libraryIndex + (event.key === Qt.Key_Up ? -1 : 1)))
                            if (event.modifiers & (Qt.ControlModifier | Qt.MetaModifier)) library.chooseCard(next)
                            else library.chooseCardWithModifiers(next, event.modifiers)
                            event.accepted = true
                        }
                    }
                    Label { parent: cardList; visible: cardList.count === 0 && (!library.searchEngine.busy || !library.searchingCards); anchors.centerIn: parent; text: library.activeFilterCount > 0 ? "No cards match these filters." : ui.cardSearchText ? "No cards match this search." : ui.selectedDeck.id ? "This deck has no cards yet." : "No cards in the library yet."; color: Theme.inkMuted; wrapMode: Text.Wrap }
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
            visible: !library.cardsVisible
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
    CardFilterPopup {
        id: cardFilters
        parent: cardFilterButton
        property real availableBelow: 520
        width: Math.min(380, library.width - 24)
        height: Math.min(520, availableBelow)
        x: cardFilterButton.width - width
        y: cardFilterButton.height + 8
        margins: 12
        function fitBelowButton() {
            const top = cardFilterButton.mapToItem(library, 0, cardFilterButton.height + 8).y
            availableBelow = Math.max(0, library.height - top - 12)
        }
        onAboutToShow: fitBelowButton()
        Connections {
            target: library
            function onHeightChanged() { if (cardFilters.visible) Qt.callLater(cardFilters.fitBelowButton) }
            function onWidthChanged() { if (cardFilters.visible) Qt.callLater(cardFilters.fitBelowButton) }
        }
        tags: library.availableTags
        selectedTags: library.filterTags
        selectedKind: library.filterKind
        selectedState: library.filterState
        scopeLabel: ui.selectedDeck.id ? ui.selectedDeck.name : "All decks"
        onTagsChosen: function(tags) { library.filterTags = tags }
        onKindChosen: function(kind) { library.filterKind = kind }
        onStateChosen: function(state) { library.filterState = state }
        onResetRequested: library.clearFilters()
        onClosed: if (library.visible && library.cardsVisible) cardFilterButton.forceActiveFocus()
    }
    Rectangle {
        visible: library.dragging
        z: 100
        x: Math.max(0, Math.min(library.width - width, library.dragPosition.x + 18))
        y: Math.max(0, Math.min(library.height - height, library.dragPosition.y + 26))
        width: Math.min(260, dragLabel.implicitWidth + 28)
        height: 38
        radius: 8
        color: Theme.surfaceRaised
        border.width: 1
        border.color: Theme.rule
        Label {
            id: dragLabel
            anchors.fill: parent; anchors.margins: 12
            verticalAlignment: Text.AlignVCenter
            textFormat: Text.PlainText
            text: library.dragKind === "deck" ? (library.ui.deckById(library.dragDeckId).name || "Deck") : library.dragCardIds.length + (library.dragCardIds.length === 1 ? " card" : " cards")
            color: Theme.ink; font.family: Theme.uiFont; font.pixelSize: 12; elide: Text.ElideRight
        }
    }
}
