pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: view
    property var changes: []
    property bool deckChanges: false
    property string expandedId: ""
    property real bodyFontSize: 14
    clip: true
    contentWidth: availableWidth
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

    function statusColor(type) {
        switch (type) {
        case "added": return Theme.success
        case "deleted": return Theme.error
        default: return Theme.warning
        }
    }
    function label(type) {
        switch (type) {
        case "added": return "Added"
        case "deleted": return "Deleted"
        default: return "Modified"
        }
    }
    function details(record) {
        if (!record || !record.id) return ""
        if (deckChanges) return "Name: " + record.name + "\nDescription: " + (record.description || "(none)")
            + "\nParent: " + (record.parentId || "Root deck")
        return "Front\n" + record.front + "\n\nBack\n" + record.back
            + "\n\nTags: " + (record.tags || "(none)") + "\nType: " + record.kind
            + "\nPoints: " + record.pointCount + "\nDeck: " + record.deckId
            + "\nNarration: " + Object.keys(record.narrations || {}).length + " recorded sides"
    }
    Column {
        width: view.availableWidth
        spacing: 4
        Repeater {
            model: view.changes
            delegate: Column {
                id: changeRow
                required property var modelData
                required property int index
                width: parent.width
                spacing: 8
                readonly property bool expanded: view.expandedId === String(modelData.id)
                ItemDelegate {
                    id: changeButton
                    width: parent.width
                    height: 54
                    onClicked: view.expandedId = changeRow.expanded ? "" : String(changeRow.modelData.id)
                    Accessible.name: view.label(changeRow.modelData.type) + ": " + titleLabel.text
                    contentItem: RowLayout {
                        spacing: 12
                        Label {
                            text: view.label(changeRow.modelData.type)
                            color: view.statusColor(changeRow.modelData.type)
                            font.pixelSize: view.bodyFontSize - 1
                            Layout.preferredWidth: 78
                        }
                        Label {
                            id: titleLabel
                            textFormat: Text.PlainText
                            text: view.deckChanges ? changeRow.modelData.name || changeRow.modelData.id
                                : String(changeRow.modelData.front || (changeRow.modelData.after || {}).front || (changeRow.modelData.before || {}).front || changeRow.modelData.id).replace(/\s+/g, " ")
                            color: Theme.ink
                            font.pixelSize: view.bodyFontSize
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Icon { kind: "chevron-right"; rotation: changeRow.expanded ? 90 : 0; width: 16; height: 16; stroke: Theme.inkMuted }
                    }
                    background: Rectangle {
                        radius: 5
                        color: changeButton.hovered || changeRow.expanded ? Theme.surfaceRaised : Theme.transparent
                        border.color: changeButton.activeFocus ? Theme.accent : Theme.rule
                        border.width: changeButton.activeFocus ? 2 : 0
                    }
                }
                RowLayout {
                    visible: changeRow.expanded
                    width: parent.width
                    spacing: 12
                    Repeater {
                        model: [{title: "Before", record: changeRow.modelData.before}, {title: "After", record: changeRow.modelData.after}]
                        delegate: Rectangle {
                            id: side
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop
                            implicitHeight: sideContent.implicitHeight + 24
                            radius: 6
                            color: Theme.editorField
                            border.color: Theme.rule
                            ColumnLayout {
                                id: sideContent
                                x: 12; y: 12; width: parent.width - 24
                                spacing: 8
                                Label { text: side.modelData.title; color: Theme.inkMuted; font.pixelSize: 12; font.bold: true }
                                TextEdit {
                                    Layout.fillWidth: true
                                    text: view.details(side.modelData.record) || "Not present"
                                    textFormat: TextEdit.PlainText
                                    color: Theme.ink
                                    font.family: Theme.monoFont
                                    font.pixelSize: 13
                                    readOnly: true
                                    selectByMouse: true
                                    wrapMode: TextEdit.Wrap
                                }
                            }
                        }
                    }
                }
                Rectangle { width: parent.width; height: 1; color: Theme.rule; opacity: 0.45 }
            }
        }
    }
}
