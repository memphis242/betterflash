pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: popup
    property var decks: []
    property string selectedDeckId: ""
    signal deckChosen(string deckId)
    readonly property var choices: {
        const query = search.text.trim().toLocaleLowerCase()
        const rows = decks.map(deck => ({id: String(deck.id), path: deckPath(deck)}))
            .filter(deck => !query || deck.path.toLocaleLowerCase().indexOf(query) >= 0)
            .sort((a, b) => a.path.localeCompare(b.path))
        return [{id: "", path: "Entire library"}].concat(rows)
    }
    padding: 14
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
    background: Rectangle {
        radius: 12
        color: Theme.surfaceRaised
        border.color: Theme.rule
    }
    function deckPath(deck) {
        const names = [String(deck.name || "Untitled deck")]
        const visited = [String(deck.id)]
        let parentId = String(deck.parentId || "")
        while (parentId && visited.indexOf(parentId) < 0) {
            visited.push(parentId)
            const ancestor = decks.find(row => String(row.id) === parentId)
            if (!ancestor) break
            names.unshift(String(ancestor.name || "Untitled deck"))
            parentId = String(ancestor.parentId || "")
        }
        return names.join(" / ")
    }
    function choose(index) {
        if (index < 0 || index >= choices.length) return
        deckChosen(choices[index].id)
        close()
    }
    onOpened: {
        deckList.currentIndex = Math.max(0, choices.findIndex(row => row.id === selectedDeckId))
        deckList.positionViewAtIndex(deckList.currentIndex, ListView.Contain)
        search.forceActiveFocus()
    }
    contentItem: ColumnLayout {
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: "Learning scope"
                color: Theme.ink
                font.family: Theme.uiFont
                font.pixelSize: 16
                font.weight: Font.DemiBold
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                elide: Text.ElideRight
            }
            GlyphButton {
                glyph: "close"
                hint: "Close deck filter"
                implicitWidth: 28
                implicitHeight: 28
                glyphSize: 14
                onClicked: popup.close()
            }
        }
        TextField {
            id: search
            Layout.fillWidth: true
            placeholderText: "Find a deck"
            Accessible.name: "Find a deck for learning statistics"
            font.family: Theme.uiFont
            font.pixelSize: 13
            onTextChanged: deckList.currentIndex = popup.choices.length > 1 ? 1 : 0
            onAccepted: popup.choose(deckList.currentIndex)
            Keys.onDownPressed: deckList.forceActiveFocus()
        }
        ListView {
            id: deckList
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            clip: true
            model: popup.choices
            spacing: 2
            boundsBehavior: Flickable.StopAtBounds
            activeFocusOnTab: true
            keyNavigationEnabled: true
            ScrollBar.vertical: ScrollBar {}
            Accessible.name: "Decks. Choose one deck and its descendants, or the entire library."
            delegate: ItemDelegate {
                id: deckRow
                required property var modelData
                required property int index
                width: deckList.width
                height: 48
                highlighted: popup.selectedDeckId === modelData.id
                onClicked: popup.choose(index)
                Accessible.name: modelData.path + (modelData.id ? ", includes subdecks" : "")
                Accessible.selected: highlighted
                contentItem: Label {
                    text: deckRow.modelData.path
                    textFormat: Text.PlainText
                    color: Theme.ink
                    font.family: Theme.uiFont
                    font.pixelSize: 13
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 6
                    color: deckRow.highlighted || deckRow.hovered ? Theme.accentSoft : Theme.transparent
                    border.width: deckRow.highlighted || deckList.activeFocus && deckList.currentIndex === deckRow.index ? 1 : 0
                    border.color: deckRow.highlighted ? Theme.accent : Theme.rule
                }
                ToolTip.visible: hovered
                ToolTip.text: modelData.path + (modelData.id ? " / Includes subdecks" : "")
            }
            Keys.onReturnPressed: popup.choose(currentIndex)
            Keys.onEnterPressed: popup.choose(currentIndex)
            Keys.onSpacePressed: popup.choose(currentIndex)
        }
        Label {
            visible: popup.height >= 230
            Layout.fillWidth: true
            text: "Each deck includes its subdecks."
            color: Theme.inkMuted
            font.family: Theme.uiFont
            font.pixelSize: 11
            wrapMode: Text.Wrap
        }
    }
}
