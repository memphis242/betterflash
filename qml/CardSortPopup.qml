pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "CardSorting.js" as CardSorting

Popup {
    id: popup
    property var sortKeys: []
    readonly property var fieldChoices: CardSorting.availableFields()
    signal keysChosen(var keys)
    padding: 16
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
    background: Rectangle { color: Theme.surfaceRaised; radius: 12; border.color: Theme.rule }
    function hasField(field) { return sortKeys.some(key => key.field === field) }
    function addField(field) {
        const keys = sortKeys.slice()
        keys.push({ field: field, direction: "descending" })
        keysChosen(CardSorting.normalizeKeys(keys))
    }
    function removeField(index) {
        const keys = sortKeys.slice()
        keys.splice(index, 1)
        keysChosen(keys)
    }
    function moveField(index, offset) {
        const target = index + offset
        if (target < 0 || target >= sortKeys.length) return
        const keys = sortKeys.slice()
        const item = keys.splice(index, 1)[0]
        keys.splice(target, 0, item)
        keysChosen(keys)
    }
    function setDirection(index, ascending) {
        const keys = sortKeys.map((key, position) => ({ field: key.field,
            direction: position === index ? (ascending ? "ascending" : "descending") : key.direction }))
        keysChosen(keys)
    }
    contentItem: ColumnLayout {
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            Label { text: "Sort cards"; color: Theme.ink; font.pixelSize: 18; font.weight: Font.DemiBold; Layout.fillWidth: true }
            GlyphButton { glyph: "close"; hint: "Close card sorting"; implicitWidth: 30; implicitHeight: 30; onClicked: popup.close() }
        }
        Flickable {
            id: sortBody
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            clip: true
            contentWidth: width
            contentHeight: sortFields.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            ColumnLayout {
                id: sortFields
                width: sortBody.width
                spacing: 12
                Label {
                    text: "Add fields in priority order. Later fields break ties in earlier fields."
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    color: Theme.inkMuted
                    Layout.fillWidth: true
                }
                Repeater {
                    model: popup.sortKeys
                    delegate: ColumnLayout {
                        id: sortRow
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: (sortRow.index + 1) + "."; color: Theme.inkMuted; font.family: Theme.monoFont }
                            Label { text: CardSorting.label(sortRow.modelData.field); color: Theme.ink; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                            GlyphButton {
                                glyph: "up"; hint: "Move " + CardSorting.label(sortRow.modelData.field) + " earlier in sort priority"
                                implicitWidth: 30; implicitHeight: 30
                                enabled: sortRow.index > 0
                                opacity: enabled ? 1 : Theme.disabledControlOpacity
                                onClicked: popup.moveField(sortRow.index, -1)
                            }
                            GlyphButton {
                                glyph: "down"; hint: "Move " + CardSorting.label(sortRow.modelData.field) + " later in sort priority"
                                implicitWidth: 30; implicitHeight: 30
                                enabled: sortRow.index + 1 < popup.sortKeys.length
                                opacity: enabled ? 1 : Theme.disabledControlOpacity
                                onClicked: popup.moveField(sortRow.index, 1)
                            }
                            GlyphButton {
                                glyph: "close"; hint: "Remove " + CardSorting.label(sortRow.modelData.field) + " from sorting"
                                implicitWidth: 30; implicitHeight: 30
                                onClicked: popup.removeField(sortRow.index)
                            }
                        }
                        AppComboBox {
                            Layout.fillWidth: true
                            model: ["Newest first", "Oldest first"]
                            currentIndex: sortRow.modelData.direction === "ascending" ? 1 : 0
                            onActivated: popup.setDirection(sortRow.index, currentIndex === 1)
                            Accessible.name: "Sort direction for " + CardSorting.label(sortRow.modelData.field)
                            comboHint: "Unknown dates sort last in either direction."
                        }
                    }
                }
                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.rule; visible: popup.sortKeys.length > 0 && popup.sortKeys.length < popup.fieldChoices.length }
                Repeater {
                    model: popup.fieldChoices
                    delegate: AppButton {
                        required property var modelData
                        visible: !popup.hasField(modelData.key)
                        text: "Add " + modelData.label.toLowerCase()
                        hint: "Use " + modelData.label.toLowerCase() + " as sort priority " + (popup.sortKeys.length + 1)
                        Layout.fillWidth: true
                        onClicked: popup.addField(modelData.key)
                    }
                }
                Label {
                    text: "Unknown dates appear last. Search relevance groups remain together."
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    color: Theme.inkMuted
                    font.pixelSize: 12
                    Layout.fillWidth: true
                }
            }
        }
        AppButton {
            visible: popup.sortKeys.length > 0
            text: "Reset sorting"
            hint: "Clear date sort keys and restore the default card order."
            Layout.fillWidth: true
            onClicked: popup.keysChosen([])
        }
    }
}
