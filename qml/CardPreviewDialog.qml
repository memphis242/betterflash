import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: preview
    objectName: "cardPreviewDialog"
    required property var ui
    property string cardId: ""
    property string deckId: ""
    readonly property var card: ui.cardById(cardId)
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(1000, ui.width - 2 * ui.gutter)
    height: Math.min(760, ui.height - 2 * ui.gutter)
    padding: width < 650 ? 14 : 24
    title: "Card preview"
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    background: Rectangle {
        radius: 16
        color: Theme.surface
        border.color: Theme.ledgerRule
    }
    Overlay.modal: Rectangle { color: Theme.scrim }

    function openFor(id) {
        cardId = id
        deckId = ui.cardById(id).deckId || ""
        open()
    }

    contentItem: ScrollView {
        id: contents
        objectName: "savedCardPreviewScroll"
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
            width: contents.availableWidth
            spacing: 16
            Label {
                Layout.fillWidth: true
                text: preview.card.id ? "Saved to " + (preview.ui.deckById(preview.deckId).name || "deck")
                    : "This card is no longer available. Close the preview and check the Library."
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                color: Theme.inkMuted
            }
            Label { text: "Front"; color: Theme.inkMuted; visible: !!preview.card.id }
            MarkdownPane {
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                markdown: preview.card.front || ""
                clozePreview: preview.card.kind === "cloze"
                mediaRoot: media.rootPath
                baseFontSize: 19
            }
            Label { text: "Back"; color: Theme.inkMuted; visible: !!preview.card.back }
            MarkdownPane {
                visible: !!preview.card.back
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                markdown: preview.card.back || ""
                mediaRoot: media.rootPath
                baseFontSize: 19
            }
        }
    }
    footer: RowLayout {
        spacing: 10
        AppButton { text: "Close"; onClicked: preview.close() }
        Item { Layout.fillWidth: true }
        AppButton {
            objectName: "previewAddAnotherCard"
            text: "Add another card"
            primary: true
            enabled: !app.busy && !!preview.ui.deckById(preview.deckId).id
            onClicked: {
                const destination = preview.deckId
                preview.close()
                preview.ui.addAnotherCard(destination)
            }
        }
    }
}
