import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: library
    required property var ui
    property bool searchVisible: ui.cardSearchText.length > 0
    readonly property bool paneVisible: ui.detailOpen && !!ui.selectedCard.id && ui.width >= 800
    readonly property var visibleCards: app.cards.filter(c => c.deckId === app.selectedDeckId && (!ui.cardSearchText || (c.front + " " + c.back + " " + c.tags).toLowerCase().indexOf(ui.cardSearchText.toLowerCase()) >= 0))
    function restoreScroll(value) {
        cardList.contentY = Math.max(0, Math.min(value, Math.max(0, cardList.contentHeight - cardList.height)))
    }
    function focusCards() {
        cardList.forceActiveFocus()
        if (cardList.currentIndex < 0 && cardList.count) {
            const index = library.visibleCards.findIndex(card => card.id === library.ui.selectedCardId)
            cardList.currentIndex = Math.max(0, index)
        }
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: library.ui.gutter
        spacing: 14
        RowLayout {
            objectName: "libraryTopBar"
            Layout.fillWidth: true
            spacing: 8
            AppButton {
                id: deckSelector
                objectName: "selectDeck"
                text: library.ui.selectedDeck.name || "Decks"
                hint: "Choose or find a deck (" + shortcuts.bindings.deckSearch + ")"
                Layout.fillWidth: true
                font.family: Theme.contentFont
                font.pixelSize: library.ui.width < 600 ? 21 : 25
                contentItem: RowLayout {
                    Label {
                        textFormat: Text.PlainText
                        text: deckSelector.text
                        font: deckSelector.font
                        color: Theme.ink
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Icon {
                        kind: "down"
                        stroke: Theme.inkMuted
                    }
                }
                onClicked: library.ui.openDeckPicker()
            }
            GlyphButton {
                objectName: "detailsToggle"
                visible: !!library.ui.selectedCard.id
                glyph: library.paneVisible ? "arrow-right" : "arrow-left"
                hint: library.paneVisible ? "Close card details" : "Open selected card details"
                onClicked: {
                    if (library.paneVisible)
                        library.ui.detailOpen = false
                    else
                        library.ui.openSelectedDetail()
                }
            }
            GlyphButton {
                objectName: "libraryMenu"
                glyph: "more"
                hint: "Library actions"
                onClicked: deckActions.open()
                Menu {
                    id: deckActions
                    y: parent.height
                    MenuItem {
                        text: "Find a card"
                        enabled: !!library.ui.selectedDeck.id
                        onTriggered: {
                            library.searchVisible = true
                            cardSearch.forceActiveFocus()
                        }
                    }
                    MenuItem {
                        text: "New deck"
                        onTriggered: library.ui.openDeckEditor("")
                    }
                    MenuItem {
                        text: "Edit deck"
                        enabled: !!library.ui.selectedDeck.id
                        onTriggered: library.ui.openDeckEditor(app.selectedDeckId)
                    }
                    MenuItem {
                        text: "Review this deck"
                        enabled: !!library.ui.selectedDeck.id
                        onTriggered: {
                            library.ui.page = 0
                            if (!app.reviewing)
                                app.startReview(app.selectedDeckId)
                        }
                    }
                    MenuSeparator {
                        visible: !!library.ui.selectedDeck.id
                    }
                    MenuItem {
                        text: "Delete deck"
                        enabled: !!library.ui.selectedDeck.id
                        onTriggered: library.ui.confirmDelete("deck", app.selectedDeckId, library.ui.selectedDeck.name)
                    }
                }
            }
            AppButton {
                objectName: "libraryPrimary"
                text: library.ui.selectedDeck.id ? "Add card" : "Add deck"
                primary: true
                onClicked: {
                    if (library.ui.selectedDeck.id)
                        library.ui.openCardEditor("")
                    else
                        library.ui.openDeckEditor("")
                }
            }
        }
        RowLayout {
            visible: library.searchVisible
            Layout.fillWidth: true
            TextField {
                id: cardSearch
                objectName: "cardSearch"
                Layout.fillWidth: true
                text: library.ui.cardSearchText
                placeholderText: "Find a card by content or tag"
                selectByMouse: true
                onTextEdited: library.ui.cardSearchText = text
                onAccepted: library.focusCards()
                Keys.onEscapePressed: {
                    library.ui.cardSearchText = ""
                    library.searchVisible = false
                    library.focusCards()
                }
            }
            GlyphButton {
                glyph: "close"
                hint: "Clear and close card search"
                onClicked: {
                    library.ui.cardSearchText = ""
                    library.searchVisible = false
                    library.focusCards()
                }
            }
        }
        RowLayout {
            id: libraryRow
            objectName: "libraryContent"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            Rectangle {
                objectName: "libraryList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.surface
                border.color: Theme.rule
                ListView {
                    id: cardList
                    objectName: "libraryScroll"
                    anchors.fill: parent
                    anchors.margins: 1
                    clip: true
                    model: library.ui.selectedDeck.id ? library.visibleCards : app.decks
                    currentIndex: -1
                    keyNavigationEnabled: true
                    highlightMoveDuration: 0
                    ScrollBar.vertical: ScrollBar {}
                    onContentYChanged: if (library.ui.page === 1 && !library.ui.restoringDeck)
                        library.ui.libraryScrollY = Math.max(0, contentY)
                    onCurrentIndexChanged: {
                        const record = model[currentIndex]
                        if (activeFocus && library.ui.selectedDeck.id && record)
                            library.ui.selectedCardId = record.id
                    }
                    Component.onCompleted: Qt.callLater(function () {
                        library.restoreScroll(library.ui.libraryScrollY)
                    })
                    delegate: ItemDelegate {
                        required property var modelData
                        required property int index
                        width: ListView.view.width
                        height: 70
                        highlighted: library.ui.selectedCardId === modelData.id || cardList.activeFocus && cardList.currentIndex === index
                        onClicked: {
                            cardList.currentIndex = index
                            if (library.ui.selectedDeck.id)
                                library.ui.pickCard(modelData.id)
                            else
                                library.ui.setDeck(modelData.id)
                        }
                        onDoubleClicked: if (library.ui.selectedDeck.id)
                            library.ui.openCardEditor(modelData.id)
                        contentItem: ColumnLayout {
                            spacing: 5
                            Label {
                                textFormat: Text.PlainText
                                text: library.ui.selectedDeck.id ? library.ui.compactText(modelData.front) : modelData.name
                                color: Theme.ink
                                elide: Text.ElideRight
                                font.family: Theme.contentFont
                                font.pixelSize: 19
                                Layout.fillWidth: true
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Label {
                                    textFormat: Text.PlainText
                                    text: library.ui.selectedDeck.id ? (modelData.kind === "reverse" ? "Both directions" : modelData.kind === "cloze" ? "Cloze" : "Basic") : (modelData.cardCount !== undefined ? modelData.cardCount + " cards" : "")
                                    color: Theme.inkMuted
                                    font.family: Theme.monoFont
                                    font.pixelSize: 10
                                    ToolTip.visible: kindHover.containsMouse
                                    ToolTip.text: library.ui.selectedDeck.id ? "Review variant type" : "Number of source cards in this deck"
                                    MouseArea {
                                        id: kindHover
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        acceptedButtons: Qt.NoButton
                                    }
                                }
                                Item {
                                    Layout.fillWidth: true
                                }
                                Label {
                                    textFormat: Text.PlainText
                                    text: modelData.dueCount !== undefined ? modelData.dueCount + " due" : (modelData.due ? "Due " + library.ui.dateTime(modelData.due) : "")
                                    color: modelData.dueCount > 0 ? Theme.warning : Theme.inkMuted
                                    font.family: Theme.monoFont
                                    font.pixelSize: 10
                                    visible: text.length > 0
                                }
                            }
                        }
                        background: Rectangle {
                            color: parent.highlighted || parent.hovered ? Theme.accentSoft : Theme.transparent
                            border.width: parent.highlighted ? 2 : 0
                            border.color: Theme.accent
                            Rectangle {
                                height: 1
                                width: parent.width
                                anchors.bottom: parent.bottom
                                color: Theme.rule
                            }
                        }
                    }
                    Keys.onReturnPressed: {
                        const record = model[currentIndex]
                        if (record) {
                            if (library.ui.selectedDeck.id)
                                library.ui.pickCard(record.id)
                            else
                                library.ui.setDeck(record.id)
                        }
                    }
                    Keys.onEnterPressed: {
                        const record = model[currentIndex]
                        if (record) {
                            if (library.ui.selectedDeck.id)
                                library.ui.pickCard(record.id)
                            else
                                library.ui.setDeck(record.id)
                        }
                    }
                    Label {
                        textFormat: Text.PlainText
                        visible: cardList.count === 0
                        anchors.fill: parent
                        anchors.margins: 24
                        text: library.ui.cardSearchText ? "No cards match this search." : library.ui.selectedDeck.id ? "Cards for this deck will appear here. Use Add card to write the first one." : "Your decks will appear here. Use Add deck to begin."
                        color: Theme.inkMuted
                        wrapMode: Text.Wrap
                    }
                }
            }
            Item {
                id: handle
                objectName: "detailsHandle"
                visible: library.paneVisible
                Layout.preferredWidth: 12
                Layout.fillHeight: true
                activeFocusOnTab: true
                Accessible.name: "Resize card details. Left and right arrows adjust width; Home restores the default."
                Rectangle {
                    width: handle.activeFocus ? 3 : 1
                    height: 50
                    anchors.centerIn: parent
                    color: handle.activeFocus ? Theme.accent : Theme.rule
                }
                Keys.onLeftPressed: library.ui.setDetailWidth(library.ui.detailWidth() + 24)
                Keys.onRightPressed: library.ui.setDetailWidth(library.ui.detailWidth() - 24)
                Keys.onPressed: function (event) {
                    if (event.key === Qt.Key_Home) {
                        library.ui.setDetailWidth(380)
                        event.accepted = true
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.SizeHorCursor
                    property real startX: 0
                    property real startWidth: 0
                    onPressed: function (mouse) {
                        handle.forceActiveFocus()
                        startX = mapToItem(libraryRow, mouse.x, mouse.y).x
                        startWidth = library.ui.detailWidth()
                    }
                    onPositionChanged: function (mouse) {
                        if (pressed)
                            library.ui.setDetailWidth(startWidth - (mapToItem(libraryRow, mouse.x, mouse.y).x - startX))
                    }
                }
            }
            Rectangle {
                id: detailPane
                objectName: "cardDetails"
                visible: library.paneVisible
                Layout.preferredWidth: Math.min(library.ui.detailWidth(), Math.max(260, libraryRow.width * 0.55))
                Layout.fillHeight: true
                color: Theme.surface
                border.color: Theme.rule
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            textFormat: Text.PlainText
                            text: "Card"
                            color: Theme.inkMuted
                            Layout.fillWidth: true
                        }
                        AppButton {
                            objectName: "editSelectedCard"
                            text: "Edit"
                            hint: shortcuts.bindings.editCard
                            onClicked: library.ui.openCardEditor(library.ui.selectedCardId)
                        }
                        GlyphButton {
                            glyph: "more"
                            hint: "Card actions"
                            onClicked: cardActions.open()
                            Menu {
                                id: cardActions
                                y: parent.height
                                MenuItem {
                                    text: "Atomicize card"
                                    onTriggered: library.ui.openAtomicize(library.ui.selectedCardId)
                                }
                                MenuItem {
                                    text: "Delete card"
                                    onTriggered: library.ui.confirmDelete("card", library.ui.selectedCardId, library.ui.compactText(library.ui.selectedCard.front).slice(0, 80))
                                }
                            }
                        }
                        GlyphButton {
                            objectName: "detailCollapse"
                            glyph: "arrow-right"
                            hint: "Close card details"
                            onClicked: library.ui.detailOpen = false
                        }
                    }
                    ScrollView {
                        objectName: "detailsScroll"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentWidth: availableWidth
                        clip: true
                        ColumnLayout {
                            width: parent.width
                            spacing: 14
                            Label {
                                textFormat: Text.PlainText
                                text: "Front"
                                color: Theme.inkMuted
                                font.family: Theme.monoFont
                                font.pixelSize: 11
                            }
                            MarkdownPane {
                                markdown: library.ui.selectedCard.front || ""
                                mediaRoot: media.rootPath
                                Layout.fillWidth: true
                                Layout.preferredHeight: implicitHeight
                                baseFontSize: 18
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                height: 1
                                color: Theme.rule
                                visible: !!library.ui.selectedCard.back
                            }
                            Label {
                                textFormat: Text.PlainText
                                text: "Back"
                                color: Theme.inkMuted
                                font.family: Theme.monoFont
                                font.pixelSize: 11
                                visible: !!library.ui.selectedCard.back
                            }
                            MarkdownPane {
                                markdown: library.ui.selectedCard.back || ""
                                mediaRoot: media.rootPath
                                Layout.fillWidth: true
                                Layout.preferredHeight: implicitHeight
                                baseFontSize: 18
                                visible: !!library.ui.selectedCard.back
                            }
                            Label {
                                textFormat: Text.PlainText
                                text: library.ui.selectedCard.tags || ""
                                visible: text.length > 0
                                color: Theme.inkMuted
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }
                        }
                    }
                    AppButton {
                        objectName: "atomicizeSelectedCard"
                        text: "Atomicize"
                        hint: "Ask the language model whether this saved card contains independent learning objectives"
                        onClicked: library.ui.openAtomicize(library.ui.selectedCardId)
                    }
                    AppButton {
                        text: "Reset pane width"
                        visible: library.ui.detailWidth() !== 380
                        font.pixelSize: 11
                        onClicked: library.ui.setDetailWidth(380)
                    }
                }
            }
        }
    }
}
