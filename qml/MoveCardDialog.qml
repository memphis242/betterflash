import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    required property var ui
    property var cardIds: []
    property string selectedDeckId: ""
    property string pendingRequest: ""
    property int requestSerial: 0
    property bool submitting: false
    property string error: ""
    readonly property var decks: app.decks.map(deck => ({id: deck.id, name: deck.name, path: deckPath(deck)}))
        .sort((a, b) => a.path.localeCompare(b.path))
    readonly property string query: destination.text.trim().toLocaleLowerCase()
    readonly property var matches: decks.filter(deck => !query || deck.path.toLocaleLowerCase().indexOf(query) >= 0)
    readonly property string resolvedDeckId: {
        if (decks.some(deck => deck.id === selectedDeckId)) return selectedDeckId
        if (!query) return ""
        const exact = decks.filter(deck => deck.path.toLocaleLowerCase() === query || deck.name.toLocaleLowerCase() === query)
        return exact.length === 1 ? exact[0].id : ""
    }
    readonly property string resolvedPath: (decks.find(deck => deck.id === resolvedDeckId) || {}).path || ""
    readonly property bool changesDeck: !!resolvedDeckId && cardIds.some(id => ui.cardById(id).deckId !== resolvedDeckId)

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(600, ui.width - 2 * ui.gutter)
    height: Math.min(520, ui.height - 2 * ui.gutter)
    padding: 20
    modal: true
    closePolicy: submitting ? Popup.NoAutoClose : Popup.CloseOnEscape
    background: Rectangle { color: Theme.surface; radius: 16; border.color: Theme.ledgerRule }
    Overlay.modal: Rectangle { color: Theme.scrim }

    function deckPath(deck) {
        const names = [deck.name]
        const seen = new Set([deck.id])
        let parentId = deck.parentId
        while (parentId && !seen.has(parentId)) {
            seen.add(parentId)
            const parentDeck = ui.deckById(parentId)
            if (!parentDeck.id) break
            names.unshift(parentDeck.name)
            parentId = parentDeck.parentId
        }
        return names.join(" / ")
    }
    function openFor(ids) {
        cardIds = ids.slice()
        selectedDeckId = ""
        destination.text = ""
        error = ""
        pendingRequest = ""
        submitting = false
        open()
    }
    function submit() {
        if (!changesDeck || submitting || app.busy) return
        error = ""
        pendingRequest = "move-dialog-" + Date.now() + "-" + (++requestSerial)
        submitting = true
        if (!app.moveCards(cardIds, resolvedDeckId, pendingRequest)) {
            pendingRequest = ""
            submitting = false
            error = "CARD_MOVE: Could not start the move. Try again when the current action finishes."
        }
    }
    onOpened: destination.forceActiveFocus()
    onClosed: pendingRequest = ""
    Connections {
        target: app
        function onMoveCardsFinished(success, destinationDeckId, requestId) {
            if (!dialog.pendingRequest || requestId !== dialog.pendingRequest) return
            dialog.pendingRequest = ""
            dialog.submitting = false
            if (success) dialog.close()
            else dialog.error = "CARD_MOVE: The card was not moved. Check the error details and choose an available deck."
        }
    }

    header: Item {
        implicitHeight: 64
        RowLayout {
            anchors.fill: parent
            anchors.margins: 20
            Label { text: dialog.cardIds.length === 1 ? "Move card" : "Move " + dialog.cardIds.length + " cards"; color: Theme.ink; font.pixelSize: 22; font.bold: true; Layout.fillWidth: true }
            GlyphButton { glyph: "close"; hint: "Close move dialog"; enabled: !dialog.submitting; onClicked: dialog.close() }
        }
    }
    contentItem: ColumnLayout {
        spacing: 12
        Label { text: "Choose a destination deck"; color: Theme.inkMuted; Layout.fillWidth: true }
        TextField {
            id: destination
            Layout.fillWidth: true
            placeholderText: "Type a deck name or path"
            selectByMouse: true
            enabled: !dialog.submitting
            onTextEdited: { dialog.selectedDeckId = ""; destinations.currentIndex = dialog.matches.length ? 0 : -1 }
            onAccepted: if (dialog.changesDeck) dialog.submit()
            Keys.onDownPressed: if (destinations.count) { destinations.currentIndex = Math.max(0, destinations.currentIndex); destinations.forceActiveFocus() }
        }
        ListView {
            id: destinations
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: dialog.matches
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationEnabled: true
            enabled: !dialog.submitting
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: row
                required property var modelData
                required property int index
                width: destinations.width
                height: 42
                highlighted: modelData.id === dialog.resolvedDeckId
                onClicked: { dialog.selectedDeckId = modelData.id; destinations.currentIndex = index }
                contentItem: Label { text: row.modelData.path; textFormat: Text.PlainText; color: Theme.ink; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
                background: Rectangle {
                    radius: 5
                    color: row.highlighted || row.hovered ? Theme.accentSoft : Theme.transparent
                    border.width: row.highlighted ? 2 : destinations.activeFocus && destinations.currentIndex === row.index ? 1 : 0
                    border.color: row.highlighted ? Theme.accent : Theme.rule
                }
                ToolTip.visible: hovered
                ToolTip.text: modelData.path
            }
            Keys.onPressed: function(event) {
                if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) && dialog.matches[currentIndex]) {
                    dialog.selectedDeckId = dialog.matches[currentIndex].id
                    moveButton.forceActiveFocus()
                    event.accepted = true
                }
            }
            Label { visible: !destinations.count; anchors.centerIn: parent; text: "No matching decks"; color: Theme.inkMuted }
        }
        Label {
            Layout.fillWidth: true
            text: dialog.resolvedPath ? (dialog.changesDeck ? "To: " : "Already in: ") + dialog.resolvedPath : "Card contents and review history are preserved."
            textFormat: Text.PlainText
            color: Theme.inkMuted
            font.pixelSize: 12
            elide: Text.ElideRight
            ToolTip.visible: destinationHover.hovered
            ToolTip.text: text
            HoverHandler { id: destinationHover }
        }
        ErrorBlock { visible: dialog.error.length > 0; message: dialog.error; Layout.fillWidth: true }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            AppButton { text: "Cancel"; enabled: !dialog.submitting; onClicked: dialog.close() }
            AppButton { id: moveButton; text: dialog.submitting ? "Moving" : "Move"; primary: true; enabled: dialog.changesDeck && !dialog.submitting && !app.busy; onClicked: dialog.submit() }
        }
    }
}
