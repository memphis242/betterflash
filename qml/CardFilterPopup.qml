pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: popup
    property var tags: []
    property var selectedTags: []
    property string selectedKind: ""
    property string selectedState: ""
    property string scopeLabel: ""
    property string anchorTag: ""
    readonly property var kinds: [
        { value: "", label: "All types" }, { value: "basic", label: "Basic" },
        { value: "reverse", label: "Both directions" }, { value: "cloze", label: "Cloze" }
    ]
    readonly property var states: [
        { value: "", label: "Any review state" }, { value: "new", label: "Never reviewed" },
        { value: "reviewed", label: "Reviewed before" }, { value: "due", label: "Due now" },
        { value: "later", label: "Due later" }
    ]
    signal tagsChosen(var tags)
    signal kindChosen(string kind)
    signal stateChosen(string state)
    signal resetRequested()
    padding: 16
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { color: Theme.surfaceRaised; radius: 12; border.color: Theme.rule; border.width: 1 }
    onOpened: {
        tagList.currentIndex = Math.max(0, tags.findIndex(row => selectedTags.indexOf(row.tag) >= 0))
        filterBody.contentY = 0
        kindFilter.forceActiveFocus()
    }
    function revealField(item) {
        const position = item.mapToItem(filterBody.contentItem, 0, 0)
        const maximum = Math.max(0, filterBody.contentHeight - filterBody.height)
        if (position.y < filterBody.contentY) filterBody.contentY = Math.max(0, position.y)
        else if (position.y + item.height > filterBody.contentY + filterBody.height)
            filterBody.contentY = Math.min(maximum, position.y + item.height - filterBody.height)
    }
    function chooseTag(index, modifiers) {
        if (index < 0 || index >= tags.length) return
        const tag = tags[index].tag
        const toggle = (modifiers & (Qt.ControlModifier | Qt.MetaModifier)) !== 0
        const range = (modifiers & Qt.ShiftModifier) !== 0
        let selection = selectedTags.slice()
        const anchor = tags.findIndex(row => row.tag === anchorTag)
        if (range && anchor >= 0) {
            selection = tags.slice(Math.min(anchor, index), Math.max(anchor, index) + 1).map(row => row.tag)
        } else if (toggle) {
            const existing = selection.indexOf(tag)
            if (existing >= 0) selection.splice(existing, 1)
            else selection.push(tag)
        } else {
            selection = [tag]
            anchorTag = tag
        }
        tagsChosen(selection)
        tagList.currentIndex = index
        tagList.forceActiveFocus()
    }
    contentItem: ColumnLayout {
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            Label { text: "Filter cards"; color: Theme.ink; font.family: Theme.uiFont; font.pixelSize: 18; font.weight: Font.DemiBold; Layout.fillWidth: true }
            GlyphButton { glyph: "close"; hint: "Close card filters"; implicitWidth: 30; implicitHeight: 30; glyphSize: 15; onClicked: popup.close() }
        }
        Flickable {
            id: filterBody
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 0
            clip: true; contentWidth: width; contentHeight: filterFields.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            ColumnLayout {
                id: filterFields
                width: filterBody.width
                spacing: 12
                Label { Layout.fillWidth: true; textFormat: Text.PlainText; text: popup.scopeLabel; color: Theme.inkMuted; font.family: Theme.monoFont; font.pixelSize: 12; elide: Text.ElideRight }
                RowLayout {
                    Layout.fillWidth: true; spacing: 10
                    Label { text: "Type"; color: Theme.inkMuted; font.family: Theme.uiFont; font.pixelSize: 13; Layout.preferredWidth: 52 }
                    ComboBox {
                        id: kindFilter
                        Layout.fillWidth: true
                        onActiveFocusChanged: if (activeFocus) popup.revealField(kindFilter)
                        model: popup.kinds; textRole: "label"; valueRole: "value"
                        currentIndex: Math.max(0, popup.kinds.findIndex(row => row.value === popup.selectedKind))
                        Accessible.name: "Filter by card type"
                        onActivated: popup.kindChosen(currentValue)
                    }
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 10
                    Label { text: "Review"; color: Theme.inkMuted; font.family: Theme.uiFont; font.pixelSize: 13; Layout.preferredWidth: 52 }
                    ComboBox {
                        id: stateFilter
                        Layout.fillWidth: true
                        onActiveFocusChanged: if (activeFocus) popup.revealField(stateFilter)
                        model: popup.states; textRole: "label"; valueRole: "value"
                        currentIndex: Math.max(0, popup.states.findIndex(row => row.value === popup.selectedState))
                        Accessible.name: "Filter by review state"
                        onActivated: popup.stateChosen(currentValue)
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "Tags"; color: Theme.ink; font.family: Theme.uiFont; font.pixelSize: 14; font.weight: Font.DemiBold; Layout.fillWidth: true }
                    Label {
                        text: popup.selectedTags.length > 0 ? popup.selectedTags.length + " selected" : "Any tag"
                        color: Theme.inkMuted; font.family: Theme.monoFont; font.pixelSize: 11
                        ToolTip.visible: tagCountHover.hovered
                        ToolTip.text: "Cards may have any selected tag. Tag, type, review state, and search restrictions all apply."
                        HoverHandler { id: tagCountHover }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(84, filterBody.height - 205)
                    Layout.minimumHeight: 84
                    color: Theme.editorField; radius: 8; border.color: Theme.rule
                    ListView {
                        id: tagList
                        anchors.fill: parent; anchors.margins: 5
                        clip: true; model: popup.tags; spacing: 2
                        boundsBehavior: Flickable.StopAtBounds
                        keyNavigationEnabled: false
                        activeFocusOnTab: true
                        onActiveFocusChanged: if (activeFocus) popup.revealField(tagList)
                        ScrollBar.vertical: ScrollBar {}
                        Accessible.name: "Tags. Control-click toggles a tag; Shift-click selects a range."
                        delegate: ItemDelegate {
                            id: tagRow
                            required property var modelData
                            required property int index
                            width: tagList.width; height: 38
                            highlighted: popup.selectedTags.indexOf(modelData.tag) >= 0
                            Accessible.name: modelData.tag + ", " + modelData.count + " cards"
                            Accessible.selected: highlighted
                            contentItem: RowLayout {
                                spacing: 8
                                Label { textFormat: Text.PlainText; text: tagRow.modelData.tag; color: Theme.ink; font.family: Theme.contentFont; font.pixelSize: 13; Layout.fillWidth: true; elide: Text.ElideRight }
                                Label { text: String(tagRow.modelData.count); color: Theme.inkMuted; font.family: Theme.monoFont; font.pixelSize: 11 }
                            }
                            background: Rectangle {
                                radius: 6
                                color: tagRow.highlighted || tagMouse.containsMouse ? Theme.accentSoft : Theme.transparent
                                border.width: tagRow.highlighted || tagList.activeFocus && tagList.currentIndex === tagRow.index ? 1 : 0
                                border.color: tagRow.highlighted ? Theme.accent : Theme.rule
                            }
                            MouseArea {
                                id: tagMouse
                                anchors.fill: parent; hoverEnabled: true
                                onClicked: function(mouse) { popup.chooseTag(tagRow.index, mouse.modifiers) }
                            }
                            ToolTip.visible: tagMouse.containsMouse
                            ToolTip.text: modelData.tag + " / " + modelData.count + " cards in this scope"
                        }
                        Keys.onPressed: function(event) {
                            const toggle = (event.modifiers & (Qt.ControlModifier | Qt.MetaModifier)) !== 0
                            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                                popup.chooseTag(currentIndex, event.modifiers)
                                event.accepted = true
                            } else if (event.key === Qt.Key_Up || event.key === Qt.Key_Down || event.key === Qt.Key_Home || event.key === Qt.Key_End) {
                                const next = event.key === Qt.Key_Home ? 0 : event.key === Qt.Key_End ? count - 1
                                    : Math.max(0, Math.min(count - 1, currentIndex + (event.key === Qt.Key_Up ? -1 : 1)))
                                if (toggle) currentIndex = next
                                else popup.chooseTag(next, event.modifiers)
                                positionViewAtIndex(next, ListView.Contain)
                                event.accepted = true
                            }
                        }
                    }
                    Label { anchors.centerIn: parent; visible: popup.tags.length === 0; text: "No tags in this scope"; color: Theme.inkMuted; font.family: Theme.uiFont; font.pixelSize: 12 }
                }
                Label { Layout.fillWidth: true; text: "Ctrl-click to toggle; Shift-click for a range."; color: Theme.inkMuted; font.family: Theme.uiFont; font.pixelSize: 11; wrapMode: Text.Wrap }
            }
        }
        AppButton {
            Layout.fillWidth: true
            text: "Clear filters"
            enabled: popup.selectedTags.length > 0 || popup.selectedKind.length > 0 || popup.selectedState.length > 0
            onClicked: { popup.resetRequested(); popup.anchorTag = "" }
        }
    }
}
