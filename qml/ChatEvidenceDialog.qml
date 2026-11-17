import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    required property var ui
    property var evidence: ({})
    property bool source: false
    readonly property string sourceUrl: source ? String(evidence.url || "") : ""
    readonly property bool externalSource: /^https?:\/\/[^/\s?#@]+(?:[/?#]|$)/i.test(sourceUrl)
    readonly property string report: {
        const lines = []
        if (source) {
            if (evidence.id) lines.push("Source: " + evidence.id)
            if (evidence.title) lines.push(String(evidence.title))
            if (evidence.url) lines.push("URL: " + evidence.url)
            if (evidence.cardId) lines.push("Saved card: " + evidence.cardId)
        } else {
            if (evidence.name) lines.push("Tool: " + evidence.name)
            if (evidence.summary) lines.push(String(evidence.summary))
            if (evidence.resultCount !== undefined) lines.push("Results: " + evidence.resultCount)
        }
        if (evidence.details) lines.push(String(evidence.details))
        return lines.join("\n\n")
    }
    function openFor(record, isSource) {
        evidence = Object.assign({}, record)
        source = isSource
        open()
    }
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(640, ui.width - 2 * ui.gutter)
    height: Math.min(540, ui.height - 2 * ui.gutter)
    padding: 20
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    title: source ? "Source details" : "Retrieval details"
    background: Rectangle { radius: 12; color: Theme.surface; border.color: Theme.rule }
    Overlay.modal: Rectangle { color: Theme.scrim }
    header: RowLayout {
        spacing: 12
        Label {
            text: dialog.title
            font.pixelSize: 20
            font.bold: true
            color: Theme.ink
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            Layout.leftMargin: dialog.padding
            Layout.topMargin: dialog.padding
        }
        GlyphButton {
            glyph: "close"
            hint: "Close details"
            Layout.rightMargin: dialog.padding
            Layout.topMargin: dialog.padding
            onClicked: dialog.close()
        }
    }
    contentItem: ScrollView {
        id: viewport
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        TextArea {
            id: evidenceText
            ContextMenu.menu: TextEditingMenu { editor: evidenceText }
            text: dialog.report
            width: viewport.availableWidth
            textFormat: TextEdit.PlainText
            readOnly: true
            selectByMouse: true
            wrapMode: TextEdit.Wrap
            color: Theme.ink
            font.family: Theme.contentFont
            font.pixelSize: 15
            padding: 0
            background: null
        }
    }
    footer: GridLayout {
        columns: dialog.width < 420 ? 1 : 3
        rowSpacing: 10
        columnSpacing: 10
        AppButton {
            visible: dialog.externalSource
            text: "Open source in browser"
            hint: dialog.sourceUrl
            Layout.fillWidth: dialog.width < 420
            Layout.maximumWidth: dialog.width
            onClicked: if (dialog.externalSource) Qt.openUrlExternally(dialog.sourceUrl)
        }
        Item { visible: dialog.width >= 420; Layout.fillWidth: true }
        AppButton {
            text: "Close"
            Layout.fillWidth: dialog.width < 420
            onClicked: dialog.close()
        }
    }
}
