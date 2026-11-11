import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: preview
    objectName: "cardPreviewDialog"
    required property var ui
    property string cardId: ""
    readonly property var card: ui.cardById(cardId)
    readonly property string deckId: card.deckId || ""
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
        open()
    }

    header: Item {
        implicitHeight: 64
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: preview.padding
            anchors.rightMargin: preview.padding
            spacing: 12
            Label {
                text: preview.title
                textFormat: Text.PlainText
                font.family: Theme.uiFont
                font.pixelSize: 24
                font.bold: true
                color: Theme.ink
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            GlyphButton {
                glyph: "check"
                visible: preview.ui.hasCheckFollowUp(preview.card)
                enabled: !app.busy && !(preview.card.loadError && preview.card.loadError.code)
                hint: "Dismiss check follow-up. Keeps other tags and review schedules."
                onClicked: preview.ui.dismissCheckFollowUp(preview.cardId)
            }
            GlyphButton {
                glyph: "more"
                hint: "Card actions"
                enabled: !!preview.card.id
                onClicked: previewCardMenu.open()
                CardActionsMenu {
                    id: previewCardMenu
                    y: parent.height
                    ui: preview.ui
                    cardId: preview.cardId
                    onBeforeAction: preview.close()
                }
            }
            GlyphButton {
                glyph: "close"
                hint: "Close card preview"
                onClicked: preview.close()
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 12
        ScrollView {
            id: contents
            objectName: "savedCardPreviewScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
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
                    selectable: true
                    mediaRoot: media.rootPath
                    baseFontSize: 19
                }
                Label { text: "Back"; color: Theme.inkMuted; visible: !!preview.card.back }
                MarkdownPane {
                    visible: !!preview.card.back
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    markdown: preview.card.back || ""
                    selectable: true
                    mediaRoot: media.rootPath
                    baseFontSize: 19
                }
            }
        }
        ClozePreviewKey {
            Layout.fillWidth: true
            visible: preview.card.kind === "cloze"
        }
    }
    footer: Flow {
        spacing: 10
        AppButton { text: "Close"; onClicked: preview.close() }
        AppButton {
            objectName: "previewEditCard"
            text: "Edit card"
            enabled: !app.busy && !!preview.card.id
            onClicked: {
                const savedId = preview.cardId
                preview.close()
                preview.ui.openCardEditor(savedId, true)
            }
        }
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
