import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore
import QtQml

ApplicationWindow {
    id: window
    objectName: "mainWindow"
    visible: true
    width: 1320
    height: 860
    minimumWidth: 360
    minimumHeight: 520
    title: "BetterFlash"
    color: Theme.canvas
    font.family: Theme.uiFont
    font.pixelSize: 14
    palette.window: Theme.canvas
    palette.windowText: Theme.ink
    palette.base: Theme.canvas
    palette.alternateBase: Theme.surface
    palette.text: Theme.ink
    palette.button: Theme.surfaceRaised
    palette.buttonText: Theme.ink
    palette.highlight: Theme.accentSoft
    palette.highlightedText: Theme.ink
    palette.placeholderText: Theme.inkMuted
    palette.mid: Theme.rule
    palette.dark: Theme.rule
    palette.light: Theme.surfaceRaised
    palette.toolTipBase: Theme.surfaceRaised
    palette.toolTipText: Theme.ink

    readonly property int gutter: width < 600 ? 12 : 24
    property int page: preferences.page
    property bool navCollapsed: preferences.navCollapsed
    property string deckBrowserParentId: preferences.deckBrowserParentId
    property bool deckTreeExpanded: preferences.deckTreeExpanded
    readonly property bool effectiveNavCollapsed: navCollapsed || width < 700
    property bool detailOpen: preferences.detailOpen
    property string selectedCardId: preferences.selectedCardId
    property real libraryScrollY: preferences.libraryY
    property string cardSearchText: ""
    property string editingDeckId: ""
    property string editingDeckParentId: ""
    property string editingCardId: ""
    property bool cardInverted: false
    property bool initializingDeckParent: false
    property string localCardError: ""
    property string localDeckError: ""
    property string pendingDeckName: ""
    property var knownDeckIds: []
    property int imageCursor: 0
    property int editorSession: 0
    property int imageSession: -1
    property string globalErrorCode: ""
    property string globalErrorMessage: ""
    property string globalErrorDetails: ""
    property string rebindAction: ""
    property string rebindSequence: ""
    property string rebindError: ""
    property string credentialTarget: "voice"
    property string deleteTarget: ""
    property string deleteId: ""
    property string deleteName: ""
    property int deleteDescendantCount: 0
    property string summarizedQueue: ""
    readonly property string currentQueue: JSON.stringify(app.pendingCards.map(card => card.variantId || card.id))
    property bool restoringDeck: false
    readonly property bool modalOpen: deckDialog.visible || cardDialog.visible || partialDialog.visible || postponeDialog.visible || commandPalette.visible || deckPicker.visible || rebindDialog.visible || credentialDialog.visible || voiceConfigDialog.visible || aiConfigDialog.visible || syncConfigDialog.visible || aiDialog.visible || atomicDialog.visible || errorDialog.visible || deleteDialog.visible || resetDialog.visible || detailDialog.visible || allHistoryDialog.visible || imageDialog.visible || exportDialog.visible || importDialog.visible || review.correctionDialogVisible
    readonly property bool typing: activeFocusItem !== null && activeFocusItem.text !== undefined && activeFocusItem.cursorPosition !== undefined
    readonly property var selectedDeck: deckById(app.selectedDeckId)
    readonly property var selectedCard: cardById(selectedCardId)
    readonly property var shortcutCommands: [
        {
            action: "commandPalette",
            label: "Open command palette"
        },
        {
            action: "deckSearch",
            label: "Find a deck"
        },
        {
            action: "newCard",
            label: "New card"
        },
        {
            action: "newDeck",
            label: "New deck"
        },
        {
            action: "editCard",
            label: "Edit selected card"
        },
        {
            action: "review",
            label: "Reveal answer"
        },
        {
            action: "previousCard",
            label: "Previous review card"
        },
        {
            action: "nextCard",
            label: "Next review card"
        },
        {
            action: "gradeMissed",
            label: "Grade: missed"
        },
        {
            action: "gradePartial",
            label: "Grade: partial recall"
        },
        {
            action: "gradeHard",
            label: "Grade: hard"
        },
        {
            action: "gradeGood",
            label: "Grade: good"
        },
        {
            action: "gradeEasy",
            label: "Grade: easy"
        },
        {
            action: "defer",
            label: "Defer to the queue's end"
        },
        {
            action: "postpone",
            label: "Postpone review date"
        },
        {
            action: "pause",
            label: "Pause or resume review"
        },
        {
            action: "reviewPage",
            label: "Open Review"
        },
        {
            action: "libraryPage",
            label: "Open Library"
        },
        {
            action: "historyPage",
            label: "Open History"
        },
        {
            action: "settingsPage",
            label: "Open Settings"
        },
        {
            action: "editorSplit",
            label: "Split front and back, or move between them"
        },
        {
            action: "editorCloze",
            label: "Hide selected words (cloze)"
        },
        {
            action: "editorImage",
            label: "Insert image"
        },
        {
            action: "saveCard",
            label: "Save card"
        }
    ]
    readonly property var paletteCommands: [
        {
            action: "deckSearch",
            label: "Find a deck"
        },
        {
            action: "newCard",
            label: "New card"
        },
        {
            action: "newDeck",
            label: "New deck"
        },
        {
            action: "editCard",
            label: "Edit selected card"
        },
        {
            action: "startReview",
            label: "Start review"
        },
        {
            action: "summary",
            label: "Summarize remaining cards"
        },
        {
            action: "atomicize",
            label: "Atomicize selected card"
        },
        {
            action: "reviewPage",
            label: "Open Review"
        },
        {
            action: "libraryPage",
            label: "Open Library"
        },
        {
            action: "historyPage",
            label: "Open History"
        },
        {
            action: "settingsPage",
            label: "Open Settings"
        }
    ]
    Settings {
        id: preferences
        category: "layout"
        property int page: 0
        property bool navCollapsed: false
        property string deckBrowserParentId: ""
        property bool deckTreeExpanded: false
        property bool detailOpen: false
        property int detailWidth: 380
        property string selectedDeckId: ""
        property string selectedCardId: ""
        property real libraryY: 0
        property int settingsSection: 0
        property string deckContexts: "{}"
        property string pinnedDecks: "[]"
    }
    onPageChanged: {
        preferences.page = page
        if (page !== 0 && app.reviewing) {
            app.pauseReview()
            voice.enabled = false
        }
    }
    onNavCollapsedChanged: preferences.navCollapsed = navCollapsed
    onDeckBrowserParentIdChanged: preferences.deckBrowserParentId = deckBrowserParentId
    onDeckTreeExpandedChanged: preferences.deckTreeExpanded = deckTreeExpanded
    onDetailOpenChanged: preferences.detailOpen = detailOpen
    onSelectedCardIdChanged: preferences.selectedCardId = selectedCardId
    onLibraryScrollYChanged: preferences.libraryY = libraryScrollY

    function deckById(id) {
        for (const d of app.decks)
            if (d.id === id)
                return d
        return ({})
    }
    function cardById(id) {
        for (const c of app.cards)
            if (c.id === id)
                return c
        return ({})
    }
    function compactText(source) {
        const paragraph = String(source || "").split(/\n\s*\n/)[0]
        return paragraph.replace(/\{\{c[1-9]\d*::([\s\S]+?)\}\}/g, function (marker, body) {
            const delimiters = /(\\*)::/g;
            let end = body.length
            let match
            while ((match = delimiters.exec(body)) !== null) {
                if (match[1].length % 2 === 0) {
                    end = match.index + match[1].length
                    break
                }
            }
            return body.slice(0, end).replace(/(\\+)::/g, function (separator, run) {
                return run.length % 2 ? "\\".repeat((run.length - 1) / 2) + "::" : separator
            })
        }).replace(/!\[([^\]]*)\]\([^)]*\)/g, "$1").replace(/[#*_`{}]/g, "").replace(/\s+/g, " ").trim()
    }
    function dateTime(value) {
        const d = new Date(value)
        return isNaN(d.getTime()) ? String(value || "") : Qt.formatDateTime(d, "yyyy-MM-dd HH:mm")
    }
    function localPath(url) {
        return decodeURIComponent(String(url).replace(/^file:\/\//, ""))
    }
    function gradeLabel(value) {
        return typeof value === "number" ? ["Missed", "Partial", "Hard", "Good", "Easy"][value] || String(value) : String(value || "")
    }
    function actionLabel(action) {
        for (const c of shortcutCommands)
            if (c.action === action)
                return c.label
        return action
    }
    function pinnedDeckIds() {
        try {
            return JSON.parse(preferences.pinnedDecks)
        } catch (e) {
            return []
        }
    }
    function deckIsPinned(id) {
        return pinnedDeckIds().indexOf(id) >= 0
    }
    function toggleDeckPin(id) {
        const pins = pinnedDeckIds()
        const index = pins.indexOf(id)
        if (index >= 0)
            pins.splice(index, 1)
        else
            pins.push(id)
        preferences.pinnedDecks = JSON.stringify(pins)
    }
    function deckChoices(query, showAll) {
        const pins = pinnedDeckIds()
        const text = query.toLowerCase().trim()
        const decks = app.decks.filter(d => !text || (d.name + " " + (d.description || "")).toLowerCase().indexOf(text) >= 0)
        decks.sort((a, b) => {
            const pa = pins.indexOf(a.id) >= 0, pb = pins.indexOf(b.id) >= 0
            if (pa !== pb)
                return pa ? -1 : 1
            return String(b.createdAt || "").localeCompare(String(a.createdAt || "")) || a.name.localeCompare(b.name)
        })
        return text || showAll ? decks : decks.filter((d, i) => pins.indexOf(d.id) >= 0 || i < 6)
    }
    function setDeck(id, stayOnPage) {
        if (id === app.selectedDeckId) {
            if (!stayOnPage)
                page = 1
            return
        }
        let contexts = ({})
        try {
            contexts = JSON.parse(preferences.deckContexts)
        } catch (e) {}
        if (app.selectedDeckId)
            contexts[app.selectedDeckId] = {
                card: selectedCardId,
                detail: detailOpen,
                scroll: libraryScrollY,
                search: cardSearchText
            }
        preferences.deckContexts = JSON.stringify(contexts)
        const saved = contexts[id] || ({})
        restoringDeck = true
        app.selectedDeckId = id
        preferences.selectedDeckId = id
        selectedCardId = saved.card || ""
        detailOpen = saved.detail || false
        cardSearchText = saved.search || ""
        libraryScrollY = saved.scroll || 0
        Qt.callLater(function () {
            library.restoreScroll(window.libraryScrollY)
            window.restoringDeck = false
        })
        if (!stayOnPage)
            page = 1
    }
    function pickCard(id) {
        selectedCardId = id
        detailOpen = true
        if (width < 800)
            detailDialog.open()
    }
    function deckIsDescendant(id, ancestorId) {
        let current = deckById(id)
        const visited = []
        while (current && current.id && current.parentId && visited.indexOf(current.id) < 0) {
            if (current.parentId === ancestorId)
                return true
            visited.push(current.id)
            current = deckById(current.parentId)
        }
        return false
    }
    function deckParentChoices() {
        return [{ id: "", name: "Top level" }].concat(app.decks.filter(d => d.id !== editingDeckId && !deckIsDescendant(d.id, editingDeckId)))
    }
    function openDeckEditor(id, parentId) {
        initializingDeckParent = true
        editingDeckId = id || ""
        editingDeckParentId = parentId || ""
        deckDialog.open()
    }
    function openCardEditor(id) {
        if (!app.decks.length) {
            openDeckEditor("")
            return
        }
        editingCardId = id || ""
        cardDialog.open()
    }
    function editingCard() {
        return cardById(editingCardId)
    }
    function splitSource(source) {
        const text = source.replace(/\r\n?/g, "\n")
        const match = /(^|\n)---[ \t]*(?=\n|$)/.exec(text)
        if (!match)
            return {
                front: text.trim(),
                back: "",
                start: -1,
                after: -1
            }
        const start = match.index + match[1].length
        const end = match.index + match[0].length
        return {
            front: text.slice(0, start).trim(),
            back: text.slice(end).trim(),
            start: start,
            after: end + (text.charAt(end) === "\n" ? 1 : 0)
        }
    }
    function splitOrMove() {
        const parts = splitSource(editorSource.text)
        if (parts.start >= 0)
            editorSource.cursorPosition = editorSource.cursorPosition < parts.start ? parts.after : 0
        else {
            const at = editorSource.cursorPosition
            const insertion = "\n\n---\n\n"
            editorSource.insert(at, insertion)
            editorSource.cursorPosition = at + insertion.length
        }
        editorSource.forceActiveFocus()
    }
    function hideSelection() {
        if (!editorSource.selectedText.length) {
            localCardError = "CLOZE_SELECTION: Select the words to hide, then try again."
            editorSource.forceActiveFocus()
            return
        }
        const parts = splitSource(editorSource.text)
        if (parts.start >= 0 && editorSource.selectionEnd > parts.start) {
            localCardError = "CLOZE_SELECTION: Select words on the front side."
            editorSource.forceActiveFocus()
            return
        }
        const selected = editorSource.selectedText
        const at = editorSource.selectionStart
        const expression = /\{\{c(\d+)::/g
        let maximum = 0
        let match
        while ((match = expression.exec(editorSource.text)) !== null)
            maximum = Math.max(maximum, Number(match[1]))
        if (maximum >= 999) {
            localCardError = "CLOZE_LIMIT: Use an existing cloze group, or create another card."
            return
        }
        const escaped = selected.replace(/(\\*)::/g, function (separator, run) {
            return "\\".repeat(run.length * 2 + 1) + "::"
        })
        const insertion = "{{c" + (maximum + 1) + "::" + escaped + "}}"
        editorSource.remove(editorSource.selectionStart, editorSource.selectionEnd)
        editorSource.insert(at, insertion)
        editorSource.cursorPosition = at + insertion.length
        cardKind.currentIndex = 1
        localCardError = ""
        editorSource.forceActiveFocus()
    }
    function chooseImage() {
        if (media.busy) {
            localCardError = "IMAGE_BUSY: Wait for the current image to finish importing."
            return
        }
        imageCursor = editorSource.cursorPosition
        imageSession = editorSession
        imageDialog.open()
    }
    function submitCard() {
        const parts = splitSource(editorSource.text)
        const cloze = /\{\{c\d+::/.test(parts.front)
        const kind = cloze || cardKind.currentValue === "cloze" ? "cloze" : cardInverted ? "reverse" : "basic"
        if (!cardDeck.currentValue) {
            localCardError = "CARD_DECK: Choose a deck for this card."
            return
        }
        if (!parts.front) {
            localCardError = "CARD_FRONT: Write a question before the separator."
            return
        }
        if (kind !== "cloze" && !parts.back) {
            localCardError = "CARD_BACK: Add the answer after a standalone --- line."
            return
        }
        if (app.saveCard(editingCardId, cardDeck.currentValue, kind, parts.front, parts.back, cardTags.text, cardPoints.value)) {
            if (cardDeck.currentValue !== app.selectedDeckId)
                setDeck(cardDeck.currentValue)
            cardDialog.close()
        }
    }
    function submitDeck() {
        if (!deckName.text.trim()) {
            localDeckError = "DECK_NAME: Enter a deck name."
            deckName.forceActiveFocus()
            return
        }
        if (editingDeckId)
            app.updateDeck(editingDeckId, deckName.text.trim(), deckDescription.text, deckParent.currentValue || "")
        else {
            knownDeckIds = app.decks.map(deck => deck.id)
            pendingDeckName = deckName.text.trim()
            app.createDeck(deckName.text.trim(), deckDescription.text, deckParent.currentValue || "")
        }
        deckDialog.close()
    }
    function confirmDelete(target, id, name) {
        deleteTarget = target
        deleteId = id
        deleteName = name
        deleteDescendantCount = target === "deck" ? app.decks.filter(d => d.id !== id && deckIsDescendant(d.id, id)).length : 0
        deleteDialog.open()
    }
    function confirmReset(target, id, name) {
        if (!app.developmentToolsEnabled || app.busy || !id)
            return
        resetDialog.target = target
        resetDialog.targetId = id
        resetDialog.targetName = name
        resetDialog.submitted = false
        resetDialog.resumeOnCancel = app.reviewing && !app.paused
        if (resetDialog.resumeOnCancel)
            app.pauseReview()
        resetDialog.open()
    }
    function openCredentials(target) {
        credentialTarget = target
        credentialDialog.open()
    }
    function showError(code, message, details) {
        globalErrorCode = code
        globalErrorMessage = message
        globalErrorDetails = details || ""
        errorDialog.open()
    }
    function shortcutAllowed(action) {
        if (action === "editorSplit" || action === "editorCloze" || action === "editorImage" || action === "saveCard")
            return cardDialog.visible && !imageDialog.visible && !errorDialog.visible && (action === "saveCard" || editorTabs.currentIndex === 0)
        if (modalOpen)
            return false
        switch (action) {
        case "review":
        case "previousCard":
        case "nextCard":
        case "gradeMissed":
        case "gradePartial":
        case "gradeHard":
        case "gradeGood":
        case "gradeEasy":
        case "defer":
        case "postpone":
        case "pause":
            if (page !== 0 || typing || !app.reviewing || app.busy)
                return false
            if (action === "pause")
                return true
            if (app.paused || !app.currentCard.id)
                return false
            if ((action === "previousCard" || action === "nextCard") && review.timelineScrollHasFocus)
                return false
            if ((action === "defer" || action === "postpone") && app.reviewingCompletedCard)
                return false
            if (action === "review" && (review.previewHasFocus || review.reviewUtilityHasFocus
                || activeFocusItem && activeFocusItem.pressed !== undefined && activeFocusItem.checkable !== undefined))
                return false
            return action.indexOf("grade") !== 0 || app.answerRevealed
        default:
            return true
        }
    }
    function runAction(action) {
        switch (action) {
        case "commandPalette":
            commandPalette.open()
            break
        case "deckSearch":
            page = 1
            deckPicker.open()
            break
        case "newCard":
            openCardEditor("")
            break
        case "newDeck":
            openDeckEditor("")
            break
        case "editCard":
            {
                const id = page === 0 && app.currentCard.id ? (app.currentCard.cardId || app.currentCard.id) : selectedCardId
                if (id)
                    openCardEditor(id)
                else {
                    page = 1
                    library.focusCards()
                }
                break
            }
        case "startReview":
            page = 0
            if (!app.reviewing)
                app.startReview(app.selectedDeckId)
            else {
                if (app.paused)
                    app.resumeReview()
            }
            break
        case "summary":
            if (app.reviewing)
                aiDialog.open()
            break
        case "atomicize":
            openAtomicize(selectedCardId)
            break
        case "reviewPage":
            page = 0
            break
        case "libraryPage":
            page = 1
            break
        case "historyPage":
            page = 2
            break
        case "settingsPage":
            page = 3
            break
        case "review":
            app.revealAnswer()
            break
        case "previousCard":
            app.navigateReview(-1)
            break
        case "nextCard":
            app.navigateReview(1)
            break
        case "gradeMissed":
            app.grade(0)
            break
        case "gradePartial":
            partialDialog.open()
            break
        case "gradeHard":
            app.grade(2)
            break
        case "gradeGood":
            app.grade(3)
            break
        case "gradeEasy":
            app.grade(4)
            break
        case "defer":
            app.deferCard()
            break
        case "postpone":
            postponeDialog.open()
            break
        case "pause":
            if (app.paused)
                app.resumeReview()
            else
                app.pauseReview()
            break
        case "editorSplit":
            splitOrMove()
            break
        case "editorCloze":
            hideSelection()
            break
        case "editorImage":
            chooseImage()
            break
        case "saveCard":
            submitCard()
            break
        }
    }
    function paletteResults(query) {
        const text = query.trim().toLowerCase()
        const actions = paletteCommands.filter(c => c.label.toLowerCase().indexOf(text) >= 0 && (c.action !== "summary" || app.reviewing) && (c.action !== "atomicize" || !!selectedCard.id))
        const decks = app.decks.filter(d => d.name.toLowerCase().indexOf(text) >= 0).map(d => ({
                    action: "deck:" + d.id,
                    label: d.name,
                    deck: true
                }))
        const cards = text.length >= 2 ? app.cards.filter(c => (c.front + " " + c.back + " " + c.tags).toLowerCase().indexOf(text) >= 0).map(c => ({
                    action: "card:" + c.id,
                    label: compactText(c.front),
                    card: true,
                    deckId: c.deckId
                })) : []
        return actions.concat(decks, cards)
    }
    function choosePaletteResult() {
        const result = paletteList.model[paletteList.currentIndex]
        if (!result)
            return
        commandPalette.close()
        if (result.deck)
            setDeck(result.action.slice(5))
        else if (result.card) {
            setDeck(result.deckId)
            pickCard(result.action.slice(5))
        } else
            Qt.callLater(function () {
                runAction(result.action)
            })
    }
    function chooseDeckResult() {
        const d = deckResultList.model[deckResultList.currentIndex]
        if (d) {
            setDeck(d.id)
            deckPicker.close()
        }
    }

    header: ToolBar {
        objectName: "globalHeader"
        height: 56
        background: Rectangle {
            color: Theme.canvas
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.rule
            }
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: window.gutter
            anchors.rightMargin: window.gutter
            spacing: 10
            Item {
                objectName: "appMark"
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                BrandMark { anchors.fill: parent }
            }
            Item {
                Layout.fillWidth: true
            }
            Label {
                textFormat: Text.PlainText
                visible: window.width >= 700
                text: app.statusMessage
                color: Theme.inkMuted
                font.family: Theme.monoFont
                font.pixelSize: 11
                elide: Text.ElideRight
                Layout.maximumWidth: 360
            }
            GlyphButton {
                objectName: "commandPaletteButton"
                glyph: "keyboard"
                hint: "Command palette (" + shortcuts.bindings.commandPalette + ")"
                onClicked: commandPalette.open()
            }
            GlyphButton {
                objectName: "themeToggle"
                glyph: Theme.dark ? "sun" : "moon"
                hint: Theme.dark ? "Switch to light theme" : "Switch to dark theme"
                onClicked: Theme.toggle()
            }
        }
    }
    RowLayout {
        id: mainContent
        objectName: "mainContent"
        anchors.fill: parent
        spacing: 0
        SidebarNavigation {
            id: navigation
            objectName: "sideNavigation"
            Layout.fillHeight: true
            Layout.preferredWidth: window.effectiveNavCollapsed ? 76 : 218
            collapsed: window.effectiveNavCollapsed
            toggleVisible: window.width >= 700
            currentIndex: window.page
            bindings: shortcuts.bindings
            onToggleRequested: window.navCollapsed = !window.navCollapsed
            onPageRequested: index => window.page = index
        }
        StackLayout {
            id: pageStack
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: window.page
            ReviewPage { id: review; ui: window; objectName: "reviewPage" }
            LibraryPage { id: library; ui: window; objectName: "libraryPage" }
            HistoryPage { ui: window; objectName: "historyPage" }
            SettingsPage { id: settings; ui: window; objectName: "settingsPage" }
        }
    }

    component Sheet: Dialog {
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(560, window.width - 2 * window.gutter)
        height: Math.min(implicitHeight, window.height - 2 * window.gutter)
        padding: window.width < 600 ? 14 : 20
        closePolicy: Popup.CloseOnEscape
        background: Rectangle {
            color: Theme.surface
            radius: 6
            border.color: Theme.rule
        }
        Overlay.modal: Rectangle {
            color: Theme.scrim
        }
    }
    Sheet {
        id: commandPalette
        objectName: "commandPalette"
        title: "Commands and decks"
        width: Math.min(650, window.width - 2 * window.gutter)
        height: Math.min(550, window.height - 40)
        onOpened: {
            commandSearch.text = ""
            paletteList.currentIndex = 0
            commandSearch.forceActiveFocus()
        }
        standardButtons: Dialog.Close
        contentItem: ColumnLayout {
            spacing: 12
            TextField {
                id: commandSearch
                objectName: "commandSearch"
                Layout.fillWidth: true
                placeholderText: "Find an action or deck"
                selectByMouse: true
                onTextChanged: paletteList.currentIndex = 0
                Keys.onDownPressed: paletteList.currentIndex = Math.min(paletteList.count - 1, paletteList.currentIndex + 1)
                Keys.onUpPressed: paletteList.currentIndex = Math.max(0, paletteList.currentIndex - 1)
                onAccepted: choosePaletteResult()
            }
            ListView {
                id: paletteList
                objectName: "commandResults"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: window.paletteResults(commandSearch.text)
                keyNavigationEnabled: true
                ScrollBar.vertical: ScrollBar {}
                delegate: ItemDelegate {
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    height: 48
                    highlighted: index === paletteList.currentIndex
                    onClicked: {
                        paletteList.currentIndex = index
                        choosePaletteResult()
                    }
                    contentItem: RowLayout {
                        Label {
                            textFormat: Text.PlainText
                            text: modelData.label
                            color: Theme.ink
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            textFormat: Text.PlainText
                            text: modelData.deck ? "Deck" : modelData.card ? "Card" : (shortcuts.bindings[modelData.action] || "")
                            color: Theme.inkMuted
                            font.family: Theme.monoFont
                            font.pixelSize: 11
                        }
                    }
                    background: Rectangle {
                        color: parent.highlighted || parent.hovered ? Theme.accentSoft : Theme.transparent
                        border.color: parent.highlighted ? Theme.accent : Theme.transparent
                    }
                }
                Keys.onReturnPressed: choosePaletteResult()
                Keys.onEnterPressed: choosePaletteResult()
                Label {
                    textFormat: Text.PlainText
                    visible: paletteList.count === 0
                    text: "No matching actions or decks."
                    color: Theme.inkMuted
                    anchors.centerIn: parent
                }
            }
        }
    }
    Sheet {
        id: deckPicker
        objectName: "deckPicker"
        title: "Find a deck"
        property bool showAll: false
        height: Math.min(570, window.height - 40)
        onOpened: {
            deckSearch.text = ""
            showAll = false
            deckResultList.currentIndex = 0
            deckSearch.forceActiveFocus()
        }
        standardButtons: Dialog.Close
        contentItem: ColumnLayout {
            spacing: 12
            TextField {
                id: deckSearch
                objectName: "deckSearch"
                Layout.fillWidth: true
                placeholderText: "Search decks"
                selectByMouse: true
                onTextChanged: deckResultList.currentIndex = 0
                Keys.onDownPressed: deckResultList.currentIndex = Math.min(deckResultList.count - 1, deckResultList.currentIndex + 1)
                Keys.onUpPressed: deckResultList.currentIndex = Math.max(0, deckResultList.currentIndex - 1)
                onAccepted: chooseDeckResult()
            }
            ListView {
                id: deckResultList
                objectName: "deckResults"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: window.deckChoices(deckSearch.text, deckPicker.showAll)
                ScrollBar.vertical: ScrollBar {}
                delegate: ItemDelegate {
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    height: 54
                    highlighted: index === deckResultList.currentIndex
                    onClicked: {
                        deckResultList.currentIndex = index
                        chooseDeckResult()
                    }
                    contentItem: RowLayout {
                        Label {
                            textFormat: Text.PlainText
                            text: modelData.name
                            color: Theme.ink
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                        GlyphButton {
                            glyph: "pin"
                            hint: window.deckIsPinned(modelData.id) ? "Unpin deck" : "Pin deck"
                            selected: window.deckIsPinned(modelData.id)
                            onClicked: window.toggleDeckPin(modelData.id)
                        }
                    }
                    background: Rectangle {
                        color: parent.highlighted || parent.hovered ? Theme.accentSoft : Theme.transparent
                        border.color: parent.highlighted ? Theme.accent : Theme.transparent
                    }
                }
                Keys.onReturnPressed: chooseDeckResult()
                Keys.onEnterPressed: chooseDeckResult()
                Label {
                    textFormat: Text.PlainText
                    visible: deckResultList.count === 0
                    text: app.decks.length ? "No matching decks." : "Create a deck to begin."
                    color: Theme.inkMuted
                    anchors.centerIn: parent
                }
            }
            RowLayout {
                Layout.fillWidth: true
                AppButton {
                    text: "New deck"
                    onClicked: {
                        deckPicker.close()
                        openDeckEditor("")
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
                AppButton {
                    visible: !deckSearch.text && !deckPicker.showAll && app.decks.length > deckResultList.count
                    text: "All decks"
                    onClicked: deckPicker.showAll = true
                }
            }
        }
    }
    Sheet {
        id: deckDialog
        objectName: "deckDialog"
        title: editingDeckId ? "Edit deck" : "New deck"
        onOpened: {
            const d = deckById(editingDeckId)
            localDeckError = ""
            initializingDeckParent = true
            editingDeckParentId = d.parentId || editingDeckParentId || ""
            deckName.text = d.name || ""
            deckDescription.text = d.description || ""
            deckParent.currentIndex = Math.max(0, deckParent.indexOfValue(editingDeckParentId))
            initializingDeckParent = false
            deckName.forceActiveFocus()
        }
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                textFormat: Text.PlainText
                text: "Name"
                color: Theme.inkMuted
            }
            TextField {
                id: deckName
                objectName: "deckName"
                Layout.fillWidth: true
                selectByMouse: true
                maximumLength: 256
                onAccepted: deckDescription.forceActiveFocus()
            }
            Label {
                textFormat: Text.PlainText
                text: "Description (optional)"
                color: Theme.inkMuted
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 100
                contentWidth: availableWidth
                TextArea {
                    id: deckDescription
                    objectName: "deckDescription"
                    wrapMode: TextEdit.Wrap
                    selectByMouse: true
                }
            }
            Label {
                textFormat: Text.PlainText
                text: "Parent deck (optional)"
                color: Theme.inkMuted
            }
            ComboBox {
                id: deckParent
                objectName: "deckParent"
                model: window.deckParentChoices()
                textRole: "name"
                valueRole: "id"
                Layout.fillWidth: true
                ToolTip.visible: hovered
                ToolTip.text: "Place this deck under another deck"
                onCurrentValueChanged: if (!initializingDeckParent) editingDeckParentId = currentValue || ""
            }
            ErrorBlock {
                message: localDeckError
                Layout.fillWidth: true
            }
        }
        footer: RowLayout {
            spacing: 10
            Item {
                Layout.fillWidth: true
            }
            AppButton {
                text: "Cancel"
                onClicked: deckDialog.close()
            }
            AppButton {
                objectName: "saveDeck"
                text: "Save deck"
                primary: true
                onClicked: submitDeck()
            }
        }
    }
    Sheet {
        id: cardDialog
        objectName: "cardDialog"
        title: editingCardId ? "Edit card" : "New card"
        width: Math.min(900, window.width - 2 * window.gutter)
        height: Math.min(760, window.height - 36)
        onOpened: {
            window.editorSession += 1
            const c = editingCard()
            localCardError = ""
            cardDeck.currentIndex = Math.max(0, cardDeck.indexOfValue(c.deckId || app.selectedDeckId))
            cardKind.currentIndex = c.kind === "cloze" ? 1 : 0
            cardInverted = c.kind === "reverse"
            editorSource.text = c.id ? c.front + (c.back ? "\n\n---\n\n" + c.back : "") : "\n\n---\n\n"
            cardTags.text = c.tags || ""
            cardPoints.value = c.pointCount || 1
            editorTabs.currentIndex = 0
            editorSource.cursorPosition = 0
            editorSource.forceActiveFocus()
        }
        onClosed: window.imageSession = -1
        contentItem: ColumnLayout {
            spacing: 10
            Instantiator {
                model: ["editorSplit", "editorCloze", "editorImage", "saveCard"]
                delegate: Shortcut {
                    required property string modelData
                    sequence: shortcuts.bindings[modelData] || ""
                    context: Qt.WindowShortcut
                    enabled: window.shortcutAllowed(modelData)
                    onActivated: window.runAction(modelData)
                }
            }
            RowLayout {
                Layout.fillWidth: true
                ComboBox {
                    id: cardDeck
                    objectName: "cardDeck"
                    model: app.decks
                    textRole: "name"
                    valueRole: "id"
                    Layout.fillWidth: true
                    ToolTip.visible: hovered
                    ToolTip.text: "Deck containing this card"
                }
                ComboBox {
                    id: cardKind
                    objectName: "cardKind"
                    model: [
                        {
                            label: "Basic",
                            key: "basic"
                        },
                        {
                            label: "Cloze",
                            key: "cloze"
                        }
                    ]
                    textRole: "label"
                    valueRole: "key"
                    Layout.preferredWidth: window.width < 600 ? 125 : 175
                    ToolTip.visible: hovered
                    ToolTip.text: "Choose Basic or Cloze card behavior."
                    onCurrentValueChanged: if (currentValue === "cloze") cardInverted = false
                }
            }
            RowLayout {
                Layout.fillWidth: true
                visible: cardKind.currentValue !== "cloze"
                CheckBox {
                    id: createInverted
                    objectName: "createInverted"
                    text: "Create inverted form"
                    visible: cardKind.currentValue !== "cloze"
                    checked: cardInverted
                    onToggled: cardInverted = checked
                    ToolTip.visible: hovered
                    ToolTip.text: "Schedule the card in both directions"
                }
                Item { Layout.fillWidth: true }
            }
            RowLayout {
                Layout.fillWidth: true
                TabBar {
                    id: editorTabs
                    objectName: "editorTabs"
                    Layout.fillWidth: true
                    TabButton {
                        text: "Source"
                    }
                    TabButton {
                        text: "Preview"
                    }
                }
                GlyphButton {
                    glyph: "image"
                    hint: "Insert image (" + shortcuts.bindings.editorImage + ")"
                    enabled: editorTabs.currentIndex === 0 && !media.busy
                    onClicked: chooseImage()
                }
                GlyphButton {
                    glyph: "edit"
                    hint: "Hide selected words (" + shortcuts.bindings.editorCloze + ")"
                    enabled: editorTabs.currentIndex === 0
                    onClicked: hideSelection()
                }
            }
            StackLayout {
                currentIndex: editorTabs.currentIndex
                Layout.fillWidth: true
                Layout.fillHeight: true
                ScrollView {
                    objectName: "editorScroll"
                    clip: true
                    contentWidth: availableWidth
                    TextArea {
                        id: editorSource
                        objectName: "editorSource"
                        font.family: Theme.monoFont
                        font.pixelSize: 14
                        wrapMode: TextEdit.Wrap
                        selectByMouse: true
                        textFormat: TextEdit.PlainText
                        placeholderText: "Question in Markdown\n\n---\n\nAnswer in Markdown"
                    }
                }
                ScrollView {
                    objectName: "editorPreviewScroll"
                    clip: true
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: parent.width
                        spacing: 16
                        Label {
                            textFormat: Text.PlainText
                            text: "Front"
                            color: Theme.inkMuted
                            font.family: Theme.monoFont
                            font.pixelSize: 11
                        }
                        MarkdownPane {
                            markdown: splitSource(editorSource.text).front
                            mediaRoot: media.rootPath
                            Layout.fillWidth: true
                            Layout.preferredHeight: implicitHeight
                            baseFontSize: 18
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            height: 1
                            color: Theme.rule
                        }
                        Label {
                            textFormat: Text.PlainText
                            text: "Back"
                            color: Theme.inkMuted
                            font.family: Theme.monoFont
                            font.pixelSize: 11
                        }
                        MarkdownPane {
                            markdown: splitSource(editorSource.text).back
                            mediaRoot: media.rootPath
                            Layout.fillWidth: true
                            Layout.preferredHeight: implicitHeight
                            baseFontSize: 18
                        }
                        ColumnLayout {
                            visible: cardInverted && cardKind.currentValue !== "cloze"
                            Layout.fillWidth: true
                            spacing: 8
                            Label {
                                textFormat: Text.PlainText
                                text: "Inverted front"
                                color: Theme.inkMuted
                                font.family: Theme.monoFont
                                font.pixelSize: 11
                            }
                            MarkdownPane {
                                objectName: "invertedFrontPreview"
                                markdown: "Ask the question that this answers based on deck context: \n\n" + splitSource(editorSource.text).back
                                mediaRoot: media.rootPath
                                Layout.fillWidth: true
                                Layout.preferredHeight: implicitHeight
                                baseFontSize: 18
                            }
                            Label {
                                textFormat: Text.PlainText
                                text: "Inverted back"
                                color: Theme.inkMuted
                                font.family: Theme.monoFont
                                font.pixelSize: 11
                            }
                            MarkdownPane {
                                objectName: "invertedBackPreview"
                                markdown: splitSource(editorSource.text).front
                                mediaRoot: media.rootPath
                                Layout.fillWidth: true
                                Layout.preferredHeight: implicitHeight
                                baseFontSize: 18
                            }
                        }
                    }
                }
            }
            Label {
                textFormat: Text.PlainText
                text: shortcuts.bindings.editorSplit + " separates front and back or moves between them. " + shortcuts.bindings.editorCloze + " hides a selection."
                color: Theme.inkMuted
                font.pixelSize: 11
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            RowLayout {
                Layout.fillWidth: true
                TextField {
                    id: cardTags
                    objectName: "cardTags"
                    placeholderText: "Tags, separated by commas"
                    Layout.fillWidth: true
                    selectByMouse: true
                }
                Label {
                    textFormat: Text.PlainText
                    text: "Points"
                    color: Theme.inkMuted
                    font.pixelSize: 12
                    ToolTip.visible: pointsTip.containsMouse
                    ToolTip.text: "Number of answer points used to grade partial recall"
                    MouseArea {
                        id: pointsTip
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.NoButton
                    }
                }
                SpinBox {
                    id: cardPoints
                    objectName: "cardPoints"
                    from: 1
                    to: 1000
                    value: 1
                    editable: true
                    Layout.preferredWidth: 105
                    ToolTip.visible: hovered
                    ToolTip.text: "Number of distinct points in this answer"
                }
            }
            ErrorBlock {
                message: localCardError || media.error
                Layout.fillWidth: true
            }
        }
        footer: RowLayout {
            spacing: 10
            Item {
                Layout.fillWidth: true
            }
            AppButton {
                text: "Cancel"
                onClicked: cardDialog.close()
            }
            AppButton {
                objectName: "saveCard"
                text: "Save card"
                primary: true
                hint: shortcuts.bindings.saveCard
                enabled: !media.busy
                onClicked: submitCard()
            }
        }
    }
    Sheet {
        id: partialDialog
        objectName: "partialDialog"
        title: "Partial recall"
        onOpened: {
            const fraction = app.currentCard.sessionGrade === 1 ? app.currentCard.sessionRecall : 0.5
            recalled.value = Math.round((app.currentCard.pointCount || 1) * fraction)
            recallFraction.value = fraction
        }
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                textFormat: Text.PlainText
                text: (app.currentCard.pointCount || 1) > 1 ? "How many answer points did you recall?" : "How much of the answer did you recall?"
                color: Theme.ink
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            RowLayout {
                visible: (app.currentCard.pointCount || 1) > 1
                SpinBox {
                    id: recalled
                    objectName: "recalledPoints"
                    from: 0
                    to: Math.max(1, app.currentCard.pointCount || 1)
                    editable: true
                }
                Label {
                    textFormat: Text.PlainText
                    text: "of " + (app.currentCard.pointCount || 1)
                    color: Theme.inkMuted
                    font.family: Theme.monoFont
                }
            }
            Slider {
                id: recallFraction
                objectName: "recallFraction"
                visible: (app.currentCard.pointCount || 1) <= 1
                from: 0
                to: 1
                stepSize: 0.05
                value: 0.5
                Layout.fillWidth: true
            }
            Label {
                textFormat: Text.PlainText
                visible: (app.currentCard.pointCount || 1) <= 1
                text: Math.round(recallFraction.value * 100) + " of 100 parts recalled"
                color: Theme.inkMuted
                font.family: Theme.monoFont
            }
        }
        footer: RowLayout {
            Item {
                Layout.fillWidth: true
            }
            AppButton {
                text: "Cancel"
                onClicked: partialDialog.close()
            }
            AppButton {
                objectName: "gradePartialConfirm"
                text: "Record partial recall"
                primary: true
                onClicked: {
                    const fraction = (app.currentCard.pointCount || 1) > 1 ? recalled.value / app.currentCard.pointCount : recallFraction.value
                    partialDialog.close()
                    app.grade(1, fraction)
                }
            }
        }
    }
    Sheet {
        id: postponeDialog
        objectName: "postponeDialog"
        title: "Postpone review"
        property string validation: ""
        onOpened: {
            validation = ""
            postponeDate.text = Qt.formatDate(new Date(Date.now() + 86400000), "yyyy-MM-dd")
            postponeDate.forceActiveFocus()
            postponeDate.selectAll()
        }
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                textFormat: Text.PlainText
                text: "Move this review to a later date. No grade will be recorded."
                color: Theme.inkMuted
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            TextField {
                id: postponeDate
                objectName: "postponeDate"
                Layout.fillWidth: true
                placeholderText: "YYYY-MM-DD"
                selectByMouse: true
                font.family: Theme.monoFont
            }
            ErrorBlock {
                message: postponeDialog.validation
                Layout.fillWidth: true
            }
        }
        footer: RowLayout {
            Item {
                Layout.fillWidth: true
            }
            AppButton {
                text: "Cancel"
                onClicked: postponeDialog.close()
            }
            AppButton {
                objectName: "postponeConfirm"
                text: "Postpone"
                primary: true
                onClicked: {
                    const value = postponeDate.text.trim()
                    const valid = /^\d{4}-\d{2}-\d{2}$/.test(value) && !isNaN(Date.parse(value)) && Qt.formatDate(new Date(value + "T12:00:00"), "yyyy-MM-dd") === value && value > Qt.formatDate(new Date(), "yyyy-MM-dd")
                    if (!valid) {
                        postponeDialog.validation = "POSTPONE_DATE: Enter a real date later than today, in YYYY-MM-DD format."
                        return
                    }
                    app.postponeCard(value)
                    postponeDialog.close()
                }
            }
        }
    }
    Sheet {
        id: rebindDialog
        objectName: "rebindDialog"
        title: "Change key binding"
        onOpened: {
            rebindSequence = ""
            rebindError = ""
            keyCapture.forceActiveFocus()
        }
        contentItem: ColumnLayout {
            spacing: 16
            Label {
                textFormat: Text.PlainText
                text: window.actionLabel(window.rebindAction)
                color: Theme.ink
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Label {
                textFormat: Text.PlainText
                text: "Press a key combination, then save it. Escape closes this dialog."
                color: Theme.inkMuted
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            FocusScope {
                id: keyCapture
                objectName: "keyCapture"
                Layout.fillWidth: true
                Layout.preferredHeight: 70
                focus: true
                Rectangle {
                    anchors.fill: parent
                    radius: 4
                    color: Theme.canvas
                    border.width: keyCapture.activeFocus ? 2 : 1
                    border.color: keyCapture.activeFocus ? Theme.accent : Theme.rule
                }
                Label {
                    textFormat: Text.PlainText
                    anchors.centerIn: parent
                    text: window.rebindSequence || "Press keys"
                    font.family: Theme.monoFont
                    color: Theme.ink
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: keyCapture.forceActiveFocus()
                }
                Keys.onPressed: function (event) {
                    if (event.key === Qt.Key_Escape) {
                        rebindDialog.close()
                        event.accepted = true
                        return
                    }
                    const sequence = shortcuts.sequenceForKey(event.key, event.modifiers)
                    if (sequence) {
                        window.rebindSequence = sequence
                        window.rebindError = ""
                    }
                    event.accepted = true
                }
            }
            ErrorBlock {
                message: window.rebindError
                Layout.fillWidth: true
            }
        }
        footer: RowLayout {
            Item {
                Layout.fillWidth: true
            }
            AppButton {
                text: "Cancel"
                onClicked: rebindDialog.close()
            }
            AppButton {
                objectName: "saveBinding"
                text: "Save binding"
                primary: true
                enabled: !!window.rebindSequence
                onClicked: {
                    if (shortcuts.rebind(window.rebindAction, window.rebindSequence))
                        rebindDialog.close()
                    else {
                        window.rebindError = shortcuts.lastError
                        keyCapture.forceActiveFocus()
                    }
                }
            }
        }
    }
    Sheet {
        id: credentialDialog
        objectName: "credentialDialog"
        title: credentialTarget === "voice" ? "Groq API key" : credentialTarget === "ai" ? "Language model API key" : "Sync access token"
        onOpened: {
            credentialValue.text = ""
            credentialValue.forceActiveFocus()
        }
        onClosed: credentialValue.text = ""
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                textFormat: Text.PlainText
                text: "The credential is kept in the system keyring when available. It is never shown after saving."
                color: Theme.inkMuted
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            TextField {
                id: credentialValue
                objectName: "credentialValue"
                echoMode: TextInput.Password
                selectByMouse: true
                Layout.fillWidth: true
                placeholderText: "Paste credential"
            }
        }
        footer: RowLayout {
            Item {
                Layout.fillWidth: true
            }
            AppButton {
                text: "Cancel"
                onClicked: credentialDialog.close()
            }
            AppButton {
                text: "Save credential"
                primary: true
                enabled: credentialValue.text.trim().length > 0
                onClicked: {
                    switch (credentialTarget) {
                    case "voice":
                        voice.setApiKey(credentialValue.text.trim())
                        break
                    case "ai":
                        ai.setApiKey(credentialValue.text.trim())
                        break
                    case "sync":
                        sync.setToken(credentialValue.text.trim())
                        break
                    }
                    credentialDialog.close()
                }
            }
        }
    }
    Sheet {
        id: voiceConfigDialog
        objectName: "voiceConfigDialog"
        title: "Voice connection"
        onOpened: {
            sttProvider.currentIndex = voice.sttProvider === "groq" ? 0 : 1
            sttModel.text = voice.sttModel
            localModel.text = voice.modelPath
        }
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                textFormat: Text.PlainText
                text: "Speech recognition"
                color: Theme.inkMuted
            }
            ComboBox {
                id: sttProvider
                model: [
                    {
                        label: "Groq",
                        key: "groq"
                    },
                    {
                        label: "Local test model",
                        key: "local"
                    }
                ]
                textRole: "label"
                valueRole: "key"
                Layout.fillWidth: true
            }
            Label {
                textFormat: Text.PlainText
                text: sttProvider.currentValue === "groq" ? "Model" : "Local model folder"
                color: Theme.inkMuted
            }
            TextField {
                id: sttModel
                visible: sttProvider.currentValue === "groq"
                Layout.fillWidth: true
                selectByMouse: true
                placeholderText: "whisper-large-v3-turbo"
            }
            TextField {
                id: localModel
                visible: sttProvider.currentValue === "local"
                Layout.fillWidth: true
                selectByMouse: true
                placeholderText: "/path/to/vosk/model"
            }
            Label {
                textFormat: Text.PlainText
                text: "Playback uses the system speech engine. With Groq, microphone audio is sent to Groq while voice review is enabled."
                color: Theme.inkMuted
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
        footer: RowLayout {
            Item {
                Layout.fillWidth: true
            }
            AppButton {
                text: "Cancel"
                onClicked: voiceConfigDialog.close()
            }
            AppButton {
                text: "Save connection"
                primary: true
                onClicked: {
                    voice.sttProvider = sttProvider.currentValue
                    voice.sttModel = sttModel.text.trim()
                    voice.modelPath = localModel.text.trim()
                    voiceConfigDialog.close()
                }
            }
        }
    }
    Sheet {
        id: aiConfigDialog
        objectName: "aiConfigDialog"
        title: "Language model connection"
        onOpened: {
            aiEndpoint.text = ai.endpoint
            aiModel.text = ai.model
        }
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                textFormat: Text.PlainText
                text: "Chat completions endpoint"
                color: Theme.inkMuted
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
            TextField {
                id: aiEndpoint
                objectName: "aiEndpoint"
                Layout.fillWidth: true
                selectByMouse: true
                placeholderText: "https://api.groq.com/openai/v1/chat/completions"
            }
            Label {
                textFormat: Text.PlainText
                text: "Model"
                color: Theme.inkMuted
            }
            TextField {
                id: aiModel
                objectName: "aiModel"
                Layout.fillWidth: true
                selectByMouse: true
                placeholderText: "llama-3.3-70b-versatile"
            }
        }
        footer: RowLayout {
            Item {
                Layout.fillWidth: true
            }
            AppButton {
                text: "Cancel"
                onClicked: aiConfigDialog.close()
            }
            AppButton {
                text: "Save connection"
                primary: true
                onClicked: {
                    ai.endpoint = aiEndpoint.text.trim()
                    ai.model = aiModel.text.trim()
                    aiConfigDialog.close()
                }
            }
        }
    }
    Sheet {
        id: syncConfigDialog
        objectName: "syncConfigDialog"
        title: "Sync connection"
        onOpened: syncEndpoint.text = sync.endpoint
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                textFormat: Text.PlainText
                text: "Server address"
                color: Theme.inkMuted
            }
            TextField {
                id: syncEndpoint
                objectName: "syncEndpoint"
                Layout.fillWidth: true
                selectByMouse: true
                placeholderText: "https://sync.example.com"
            }
            Label {
                textFormat: Text.PlainText
                text: "Use the same collection access token on each device."
                color: Theme.inkMuted
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
        }
        footer: RowLayout {
            Item {
                Layout.fillWidth: true
            }
            AppButton {
                text: "Cancel"
                onClicked: syncConfigDialog.close()
            }
            AppButton {
                text: "Save connection"
                primary: true
                onClicked: {
                    sync.endpoint = syncEndpoint.text.trim()
                    syncConfigDialog.close()
                }
            }
        }
    }
    Sheet {
        id: aiDialog
        objectName: "aiDialog"
        title: "Remaining cards"
        width: Math.min(760, window.width - 2 * window.gutter)
        height: Math.min(650, window.height - 40)
        onClosed: if (ai.busy)
            ai.cancel()
        contentItem: ColumnLayout {
            spacing: 14
            Label {
                textFormat: Text.PlainText
                text: "Generate a summary of the cards still in this queue. Their question prompts will be sent to " + ai.endpoint + " using " + ai.model + "."
                color: Theme.inkMuted
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Label {
                textFormat: Text.PlainText
                visible: ai.summary.length > 0 && window.summarizedQueue !== window.currentQueue
                text: "This result is from an earlier queue. Generate again to summarize the cards remaining now."
                color: Theme.warning
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            ErrorBlock {
                message: ai.error
                Layout.fillWidth: true
            }
            BusyIndicator {
                running: ai.busy
                visible: running
                Layout.alignment: Qt.AlignHCenter
            }
            ScrollView {
                objectName: "summaryScroll"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth
                MarkdownPane {
                    width: parent.width
                    markdown: ai.summary
                    mediaRoot: media.rootPath
                    baseFontSize: 18
                }
            }
            Label {
                textFormat: Text.PlainText
                visible: ai.summary.length > 0
                text: ai.status
                color: Theme.inkMuted
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                font.pixelSize: 11
            }
        }
        footer: RowLayout {
            Item {
                Layout.fillWidth: true
            }
            AppButton {
                text: ai.busy ? "Cancel request" : "Close"
                onClicked: {
                    if (ai.busy)
                        ai.cancel()
                    aiDialog.close()
                }
            }
            AppButton {
                objectName: "generateSummary"
                text: "Generate summary"
                primary: true
                enabled: !ai.busy && app.reviewing && app.queueCount > 0
                onClicked: {
                    window.summarizedQueue = window.currentQueue
                    ai.summarizeRemaining()
                }
            }
        }
    }
    Sheet {
        id: errorDialog
        objectName: "errorDialog"
        title: "Action could not complete"
        height: Math.min(430, window.height - 2 * window.gutter)
        standardButtons: Dialog.Close
        contentItem: ScrollView {
            clip: true
            contentWidth: availableWidth
            ErrorBlock {
                width: parent.width
                code: globalErrorCode
                message: globalErrorMessage
                details: globalErrorDetails
            }
        }
    }
    AtomicizeDialog {
        id: atomicDialog
        ui: window
    }
    Sheet {
        id: resetDialog
        objectName: "resetReviewDialog"
        property string target: "deck"
        property string targetId: ""
        property string targetName: ""
        property bool submitted: false
        property bool resumeOnCancel: false
        title: "Reset " + target
        implicitHeight: 300
        onClosed: if (!submitted && resumeOnCancel && app.reviewing && app.paused)
            app.resumeReview()
        contentItem: ScrollView {
            clip: true
            contentWidth: availableWidth
            Label {
                width: parent.width
                textFormat: Text.PlainText
                text: "Return \"" + resetDialog.targetName + "\" to new and due now? "
                    + (resetDialog.target === "deck" ? "This includes every card in its subdecks. " : "This includes all reverse and cloze variants. ")
                    + "Difficulty, intervals and review counts will be reset. Card content and past history remain. Any active review will end."
                color: Theme.ink
                wrapMode: Text.Wrap
            }
        }
        footer: RowLayout {
            Item { Layout.fillWidth: true }
            AppButton {
                text: "Cancel"
                onClicked: resetDialog.close()
            }
            AppButton {
                text: "Reset " + resetDialog.target
                primary: true
                enabled: !app.busy
                onClicked: {
                    resetDialog.submitted = true
                    if (resetDialog.target === "deck")
                        app.resetDeck(resetDialog.targetId)
                    else
                        app.resetCard(resetDialog.targetId)
                    resetDialog.close()
                }
            }
        }
    }
    Sheet {
        id: deleteDialog
        objectName: "deleteDialog"
        title: "Delete " + deleteTarget
        implicitHeight: 280
        height: Math.min(implicitHeight, window.height - 2 * window.gutter)
        contentItem: ScrollView {
            clip: true
            contentWidth: availableWidth
            Label {
                textFormat: Text.PlainText
                width: parent.width
                text: "Delete \"" + deleteName + "\"" + (deleteTarget === "deck" ? " and all its cards" + (deleteDescendantCount ? " and " + deleteDescendantCount + " child decks" : "") : "") + "? Completed review records will remain in history."
                color: Theme.ink
                wrapMode: Text.Wrap
            }
        }
        footer: RowLayout {
            Item {
                Layout.fillWidth: true
            }
            AppButton {
                text: "Cancel"
                onClicked: deleteDialog.close()
            }
            AppButton {
                text: "Delete " + deleteTarget
                primary: true
                onClicked: {
                    if (deleteTarget === "deck")
                        app.deleteDeck(deleteId)
                    else
                        app.deleteCard(deleteId)
                    deleteDialog.close()
                    if (deleteTarget === "card") {
                        selectedCardId = ""
                        detailOpen = false
                    }
                }
            }
        }
    }
    Sheet {
        id: detailDialog
        objectName: "cardDetailDialog"
        title: "Card"
        height: Math.min(700, window.height - 40)
        onClosed: window.detailOpen = false
        contentItem: ScrollView {
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.width
                spacing: 16
                Label {
                    textFormat: Text.PlainText
                    text: "Front"
                    color: Theme.inkMuted
                }
                MarkdownPane {
                    markdown: window.selectedCard.front || ""
                    mediaRoot: media.rootPath
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    baseFontSize: 18
                }
                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: Theme.rule
                }
                Label {
                    textFormat: Text.PlainText
                    text: "Back"
                    visible: !!window.selectedCard.back
                    color: Theme.inkMuted
                }
                MarkdownPane {
                    markdown: window.selectedCard.back || ""
                    visible: !!window.selectedCard.back
                    mediaRoot: media.rootPath
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    baseFontSize: 18
                }
            }
        }
        footer: RowLayout {
            AppButton {
                objectName: "atomicizeDetailModal"
                text: "Atomicize"
                hint: "Ask the language model whether this saved card contains independent learning objectives"
                onClicked: window.openAtomicize(selectedCardId)
            }
            Item {
                Layout.fillWidth: true
            }
            AppButton {
                text: "Close"
                onClicked: detailDialog.close()
            }
            AppButton {
                text: "Edit card"
                primary: true
                onClicked: {
                    detailDialog.close()
                    openCardEditor(selectedCardId)
                }
            }
        }
    }
    Sheet {
        id: allHistoryDialog
        objectName: "allHistoryDialog"
        title: "Review records"
        width: Math.min(950, window.width - 2 * window.gutter)
        height: Math.min(740, window.height - 40)
        standardButtons: Dialog.Close
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                textFormat: Text.PlainText
                text: app.history.length >= 200 ? "Most recent 200 reviews. Export the collection for the full history." : app.history.length + " review records"
                color: Theme.inkMuted
                font.family: Theme.monoFont
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            HistoryList {
                ui: window
                records: app.history
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
        }
    }
    FileDialog {
        id: imageDialog
        objectName: "imageDialog"
        title: "Insert image"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Images (*.png *.jpg *.jpeg *.gif *.webp)"]
        onAccepted: media.importImage(selectedFile, "image")
    }
    FileDialog {
        id: exportDialog
        objectName: "exportDialog"
        title: "Export collection"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: ["BetterFlash collection (*.json)"]
        onAccepted: app.exportCollection(localPath(selectedFile))
    }
    FileDialog {
        id: importDialog
        objectName: "importDialog"
        title: "Import collection"
        fileMode: FileDialog.OpenFile
        nameFilters: ["BetterFlash collection (*.json)"]
        onAccepted: app.importCollection(localPath(selectedFile))
    }
    Connections {
        target: media
        function onImageImported(markdown) {
            if (!cardDialog.visible || window.imageSession !== window.editorSession)
                return
            const at = Math.min(window.imageCursor, editorSource.length)
            editorSource.insert(at, "\n" + markdown + "\n")
            editorSource.cursorPosition = at + markdown.length + 2
            window.imageSession = -1
            editorSource.forceActiveFocus()
        }
    }
    Connections {
        target: app
        function onReviewingChanged() {
            if (app.reviewing)
                window.page = 0
        }
        function onDecksChanged() {
            if (window.pendingDeckName) {
                const created = app.decks.find(deck => deck.name === window.pendingDeckName && window.knownDeckIds.indexOf(deck.id) < 0)
                if (created) {
                    window.pendingDeckName = ""
                    window.setDeck(created.id)
                    return
                }
            }
            if (!app.selectedDeckId || !window.deckById(app.selectedDeckId).id) {
                const saved = window.deckById(preferences.selectedDeckId)
                if (saved.id)
                    app.selectedDeckId = saved.id
                else
                    app.selectedDeckId = app.decks.length ? app.decks[0].id : ""
            }
        }
        function onSelectedDeckIdChanged() {
            preferences.selectedDeckId = app.selectedDeckId
        }
        function onCardsChanged() {
            if (window.selectedCardId && !window.cardById(window.selectedCardId).id) {
                window.selectedCardId = ""
                window.detailOpen = false
            }
        }
        function onLastErrorChanged() {
            if (app.lastError.message) {
                window.pendingDeckName = ""
                window.showError(app.lastError.code || "APP_ERROR", app.lastError.message, app.lastError.detail || app.lastError.details || "")
            }
        }
    }
    Connections {
        target: voice
        function onErrorOccurred(code, message) {
            window.showError(code, message, "")
        }
    }
    Instantiator {
        model: window.shortcutCommands.filter(c => c.action.indexOf("editor") !== 0 && c.action !== "saveCard").map(c => c.action)
        delegate: Shortcut {
            required property string modelData
            sequence: shortcuts.bindings[modelData] || ""
            context: Qt.WindowShortcut
            enabled: window.shortcutAllowed(modelData)
            onActivated: window.runAction(modelData)
        }
    }

    function openPostpone() {
        postponeDialog.open()
    }
    function openPartial() {
        partialDialog.open()
    }
    function openSummary() {
        aiDialog.open()
    }
    function openAtomicize(cardId) {
        if (detailDialog.visible)
            detailDialog.close()
        atomicDialog.openFor(cardId || selectedCardId)
    }
    function openLanguageModelSettings() {
        page = 3
        settings.section = 3
    }
    function openDeckPicker() {
        deckPicker.open()
    }
    function openAllHistory() {
        allHistoryDialog.open()
    }
    function openRebind(action) {
        rebindAction = action
        rebindDialog.open()
    }
    function openVoiceConfig() {
        voiceConfigDialog.open()
    }
    function openAiConfig() {
        aiConfigDialog.open()
    }
    function openSyncConfig() {
        syncConfigDialog.open()
    }
    function exportData() {
        exportDialog.open()
    }
    function importData() {
        importDialog.open()
    }
    function settingsSection() {
        return preferences.settingsSection
    }
    function setSettingsSection(index) {
        preferences.settingsSection = index
    }
    function detailWidth() {
        return preferences.detailWidth
    }
    function setDetailWidth(value) {
        preferences.detailWidth = Math.round(Math.max(260, Math.min(650, value)))
    }
    function openSelectedDetail() {
        if (selectedCardId) {
            detailOpen = true
            if (width < 800)
                detailDialog.open()
        }
    }
}
