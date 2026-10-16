import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "ShortcutDescriptions.js" as ShortcutDescriptions

Item {
    id: table
    required property var commands
    required property var bindings
    property real maxBodyHeight: 10000
    signal commandActivated(string action)

    readonly property string longestAction: commands.reduce((longest, command) =>
        String(command.label || "").length > longest.length ? String(command.label || "") : longest, "")
    readonly property string longestBinding: commands.reduce((longest, command) => {
        const value = bindings[command.action] || "Unassigned"
        return String(value).length > longest.length ? String(value) : longest
    }, "")
    readonly property real actionTextWidth: actionMetrics.advanceWidth
    readonly property real bindingTextWidth: bindingMetrics.advanceWidth
    readonly property real columnPadding: 12
    readonly property real columnGap: 18
    readonly property real actionColumnWidth: actionTextWidth + 24
    readonly property real keyColumnWidth: Math.max(112, bindingTextWidth + 24)
    readonly property real preferredWidth: columnPadding * 2 + actionColumnWidth + columnGap + keyColumnWidth
    readonly property real effectiveActionWidth: Math.max(0, Math.min(actionColumnWidth,
        width - columnPadding * 2 - columnGap - keyColumnWidth))
    readonly property real rowHeight: 50
    implicitWidth: preferredWidth
    implicitHeight: header.implicitHeight + body.height

    function focusRow(targetIndex) {
        const nextIndex = Math.max(0, Math.min(commands.length - 1, targetIndex))
        body.currentIndex = nextIndex
        body.positionViewAtIndex(nextIndex, ListView.Contain)
        Qt.callLater(function () {
            if (body.currentItem)
                body.currentItem.forceActiveFocus()
        })
    }

    TextMetrics {
        id: actionMetrics
        text: table.longestAction
        font.family: Theme.uiFont
        font.pixelSize: 16
    }
    TextMetrics {
        id: bindingMetrics
        text: table.longestBinding
        font.family: Theme.monoFont
        font.pixelSize: 16
    }

    ColumnLayout {
        anchors.left: parent.left
        anchors.top: parent.top
        width: parent.width
        spacing: 0

        RowLayout {
            id: header
            Layout.fillWidth: true
            Layout.leftMargin: table.columnPadding
            Layout.rightMargin: table.columnPadding
            implicitHeight: 36
            spacing: table.columnGap
            Label {
                text: "Action"
                color: Theme.inkMuted
                font.family: Theme.uiFont
                font.pixelSize: 14
                font.bold: true
                Layout.preferredWidth: table.effectiveActionWidth
                Layout.minimumWidth: 0
                ToolTip.visible: actionHeaderHover.hovered
                ToolTip.text: "The command performed by the shortcut"
                HoverHandler { id: actionHeaderHover }
            }
            Label {
                text: "Key binding"
                color: Theme.inkMuted
                font.family: Theme.uiFont
                font.pixelSize: 14
                font.bold: true
                Layout.preferredWidth: table.keyColumnWidth
                Layout.minimumWidth: table.keyColumnWidth
                ToolTip.visible: bindingHeaderHover.hovered
                ToolTip.text: "The key combination assigned to the command"
                HoverHandler { id: bindingHeaderHover }
            }
        }

        ListView {
            id: body
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, table.maxBodyHeight)
            Layout.minimumHeight: 0
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            model: table.commands
            delegate: ItemDelegate {
                id: row
                required property var modelData
                required property int index
                width: body.width
                implicitHeight: Math.max(table.rowHeight, actionLabel.implicitHeight + 16)
                activeFocusOnTab: true
                onClicked: table.commandActivated(modelData.action)
                onActiveFocusChanged: if (activeFocus) {
                    body.currentIndex = index
                    body.positionViewAtIndex(index, ListView.Contain)
                }
                Keys.onUpPressed: {
                    table.focusRow(index - 1)
                    event.accepted = true
                }
                Keys.onDownPressed: {
                    table.focusRow(index + 1)
                    event.accepted = true
                }
                Keys.onReturnPressed: table.commandActivated(modelData.action)
                Keys.onSpacePressed: {
                    table.commandActivated(modelData.action)
                    event.accepted = true
                }
                ToolTip.visible: hovered || activeFocus
                ToolTip.text: ShortcutDescriptions.forAction(modelData.action, modelData.label)
                contentItem: RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: table.columnPadding
                    anchors.rightMargin: table.columnPadding
                    spacing: table.columnGap
                    Label {
                        id: actionLabel
                        text: modelData.label
                        textFormat: Text.PlainText
                        color: Theme.ink
                        font.family: Theme.uiFont
                        font.pixelSize: 16
                        wrapMode: Text.Wrap
                        verticalAlignment: Text.AlignVCenter
                        Layout.preferredWidth: table.effectiveActionWidth
                        Layout.minimumWidth: 0
                        Layout.fillHeight: true
                    }
                    Label {
                        text: table.bindings[modelData.action] || "Unassigned"
                        textFormat: Text.PlainText
                        color: Theme.ink
                        font.family: Theme.monoFont
                        font.pixelSize: 16
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                        Layout.preferredWidth: table.keyColumnWidth
                        Layout.minimumWidth: table.keyColumnWidth
                        Layout.fillHeight: true
                    }
                }
                background: Rectangle {
                    color: row.hovered || row.activeFocus ? Theme.accentSoft : Theme.transparent
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: Theme.rule
                    }
                }
            }
        }
    }
}
