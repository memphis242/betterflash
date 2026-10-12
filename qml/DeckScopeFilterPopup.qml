pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: popup
    property var decks: []
    property var selectedDeckIds: []
    property string anchorId: ""
    property bool anchorCheckedState: false
    readonly property int visibleRowTarget: Math.min(7, decks.length + 1)
    signal deckSelectionChanged(var deckIds)

    readonly property var choices: {
        const query = search.text.trim().toLocaleLowerCase()
        const rows = decks.map(deck => ({id: String(deck.id), path: deckPath(deck)}))
            .filter(deck => !query || deck.path.toLocaleLowerCase().indexOf(query) >= 0)
            .sort((a, b) => a.path.localeCompare(b.path))
        return [{id: "", path: "Entire library"}].concat(rows)
    }
    readonly property var normalizedSelection: {
        const available = decks.map(deck => String(deck.id))
        return selectedDeckIds.map(id => String(id)).filter((id, index, values) =>
            available.indexOf(id) >= 0 && values.indexOf(id) === index)
    }

    padding: 14
    implicitHeight: topPadding + bottomPadding + pickerHeader.implicitHeight
        + search.implicitHeight + selectionHelp.implicitHeight + 3 * pickerLayout.spacing
        + visibleRowTarget * 48 + (visibleRowTarget - 1) * deckList.spacing
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
    background: Rectangle { radius: 12; color: Theme.surfaceRaised; border.color: Theme.rule }

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
    function setSelection(selection) {
        const unique = selection.map(id => String(id)).filter((id, index, values) => id && values.indexOf(id) === index)
        deckSelectionChanged(unique)
    }
    function choose(index, modifiers) {
        if (index < 0 || index >= choices.length) return
        const choice = choices[index]
        const shift = (modifiers & Qt.ShiftModifier) !== 0
        let selection = normalizedSelection.slice()
        if (!choice.id) {
            anchorId = ""
            setSelection([])
            deckList.currentIndex = index
            deckList.forceActiveFocus()
            return
        }
        const clickedSelected = selection.indexOf(choice.id) >= 0
        const anchor = choices.findIndex(row => row.id === anchorId)
        if (shift && anchor >= 1) {
            const first = Math.min(anchor, index)
            const last = Math.max(anchor, index)
            const rangeIds = choices.slice(first, last + 1).map(row => row.id).filter(Boolean)
            if (anchorCheckedState)
                selection = selection.concat(rangeIds)
                    .filter((id, position, ids) => ids.indexOf(id) === position)
            else
                selection = selection.filter(id => rangeIds.indexOf(id) < 0)
            setSelection(selection)
        } else {
            if (clickedSelected) selection = selection.filter(id => id !== choice.id)
            else selection.push(choice.id)
            setSelection(selection)
            if ((modifiers & (Qt.ShiftModifier | Qt.ControlModifier | Qt.MetaModifier)) === 0) {
                anchorId = choice.id
                anchorCheckedState = !clickedSelected
            }
        }
        deckList.currentIndex = index
        deckList.forceActiveFocus()
    }
    onOpened: {
        deckList.currentIndex = 0
        deckList.positionViewAtIndex(deckList.currentIndex, ListView.Contain)
        anchorId = ""
        anchorCheckedState = false
        search.forceActiveFocus()
    }
    onClosed: { anchorId = ""; anchorCheckedState = false }

    contentItem: ColumnLayout {
        id: pickerLayout
        spacing: 10
        RowLayout {
            id: pickerHeader
            Layout.fillWidth: true
            Label {
                text: "Learning scope"; color: Theme.ink; font.family: Theme.uiFont; font.pixelSize: 16; font.weight: Font.DemiBold
                Layout.fillWidth: true; Layout.minimumWidth: 0; elide: Text.ElideRight
            }
            GlyphButton { glyph: "close"; hint: "Close deck filter"; implicitWidth: 28; implicitHeight: 28; glyphSize: 14; onClicked: popup.close() }
        }
        TextField {
            id: search
            Layout.fillWidth: true
            placeholderText: "Find a deck"
            Accessible.name: "Find a deck for learning statistics"
            font.family: Theme.uiFont; font.pixelSize: 13
            onTextChanged: { deckList.currentIndex = popup.choices.length > 1 ? 1 : 0; popup.anchorId = "" }
            Keys.onDownPressed: deckList.forceActiveFocus()
        }
        ListView {
            id: deckList
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 0
            clip: true; model: popup.choices; spacing: 2; boundsBehavior: Flickable.StopAtBounds
            activeFocusOnTab: true; keyNavigationEnabled: true
            ScrollBar.vertical: ScrollBar {}
            Accessible.name: "Decks. Select multiple decks; each includes its subdecks. Shift-select applies a range."
            delegate: ItemDelegate {
                id: deckRow
                required property var modelData
                required property int index
                width: deckList.width
                height: Math.max(36, Math.min(48, (deckList.height - (popup.visibleRowTarget - 1) * deckList.spacing) / popup.visibleRowTarget))
                focusPolicy: Qt.NoFocus
                highlighted: deckRow.modelData.id === "" ? popup.normalizedSelection.length === 0 : popup.normalizedSelection.indexOf(deckRow.modelData.id) >= 0
                Accessible.name: deckRow.modelData.path + (deckRow.modelData.id ? ", includes subdecks" : ", all decks")
                Accessible.role: Accessible.CheckBox
                Accessible.selected: highlighted
                Accessible.checked: highlighted
                Accessible.onToggleAction: popup.choose(deckRow.index, Qt.NoModifier)
                contentItem: RowLayout {
                    spacing: 10
                    Rectangle {
                        Layout.preferredWidth: 18; Layout.preferredHeight: 18; radius: 4
                        color: deckRow.highlighted ? Theme.accent : Theme.transparent
                        border.width: deckRow.highlighted ? 2 : 1; border.color: deckRow.highlighted ? Theme.accent : Theme.rule
                        Icon { anchors.centerIn: parent; visible: deckRow.highlighted; kind: "check"; width: 14; height: 14; stroke: Theme.canvas; lineWidth: 2 }
                    }
                    Label {
                        text: deckRow.modelData.path; textFormat: Text.PlainText; color: Theme.ink; font.family: Theme.uiFont; font.pixelSize: 13
                        wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter; Layout.fillWidth: true
                    }
                }
                background: Rectangle {
                    radius: 6; color: deckRow.highlighted || deckMouse.containsMouse ? Theme.accentSoft : Theme.transparent
                    border.width: deckRow.highlighted || deckList.activeFocus && deckList.currentIndex === deckRow.index ? 1 : 0
                    border.color: deckRow.highlighted ? Theme.accent : Theme.rule
                }
                ToolTip.visible: deckMouse.containsMouse
                ToolTip.text: modelData.path + (modelData.id ? " / Includes subdecks" : " / Clears the deck selection")
                MouseArea {
                    id: deckMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: function(mouse) { popup.choose(deckRow.index, mouse.modifiers) }
                }
            }
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                    popup.choose(currentIndex, event.modifiers); event.accepted = true
                } else if (event.key === Qt.Key_Up || event.key === Qt.Key_Down || event.key === Qt.Key_Home || event.key === Qt.Key_End) {
                    const next = event.key === Qt.Key_Home ? 0 : event.key === Qt.Key_End ? count - 1 : Math.max(0, Math.min(count - 1, currentIndex + (event.key === Qt.Key_Up ? -1 : 1)))
                    currentIndex = next; positionViewAtIndex(next, ListView.Contain); event.accepted = true
                }
            }
        }
        Label {
            id: selectionHelp
            visible: popup.height >= 230; Layout.fillWidth: true
            text: "Click to toggle; Shift-click applies a range. No decks selected means entire library."
            color: Theme.inkMuted; font.family: Theme.uiFont; font.pixelSize: 11; wrapMode: Text.Wrap
        }
    }
}
