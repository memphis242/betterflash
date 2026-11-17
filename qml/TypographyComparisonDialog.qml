pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    required property var ui
    title: "Compare reading typefaces"
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(1120, ui.width - 2 * ui.gutter)
    height: Math.min(760, ui.height - 2 * ui.gutter)
    padding: 22
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { color: Theme.surface; border.color: Theme.rule; radius: 12 }
    Overlay.modal: Rectangle { color: Theme.scrim }
    header: Label {
        text: dialog.title
        color: Theme.ink
        font.pixelSize: 22
        font.bold: true
        padding: dialog.padding
    }
    contentItem: ColumnLayout {
        spacing: 14
        Label {
            text: "Compare the same content in IBM Plex Mono and the system sans-serif face. This preview does not change the application fonts. Code, identifiers, and numbers stay monospace in both samples."
            textFormat: Text.PlainText
            color: Theme.inkMuted
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        ScrollView {
            id: samples
            contentWidth: availableWidth
            clip: true
            Layout.fillWidth: true
            Layout.fillHeight: true
            GridLayout {
                width: samples.availableWidth
                columns: width >= 760 ? 2 : 1
                columnSpacing: 24
                rowSpacing: 24
                Repeater {
                    model: ["IBM Plex Mono", "sans-serif"]
                    delegate: ColumnLayout {
                        id: sample
                        required property string modelData
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.alignment: Qt.AlignTop
                        spacing: 16
                        Label {
                            text: sample.modelData === "IBM Plex Mono" ? "Monospace / current" : "Proportional / system sans-serif"
                            font.bold: true
                            color: Theme.accent
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                        Label {
                            text: "Transaction durability"
                            font.family: sample.modelData
                            font.pixelSize: 24
                            font.bold: true
                            color: Theme.ink
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                        TextArea {
                            id: proseText
                            ContextMenu.menu: TextEditingMenu { editor: proseText }
                            text: "What does durability guarantee after a transaction commits?\n\nA committed transaction's changes survive a restart. Durability concerns the persistence of committed data; it does not mean every transaction succeeds.\n\nUse short sentences for a single claim. Keep related qualifications close to the claim they explain."
                            textFormat: TextEdit.PlainText
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.Wrap
                            font.family: sample.modelData
                            font.pixelSize: 18
                            color: Theme.ink
                            padding: 0
                            background: null
                            Layout.fillWidth: true
                        }
                        Label {
                            text: "Inline code and identifiers"
                            font.family: sample.modelData
                            color: Theme.inkMuted
                        }
                        Rectangle {
                            color: Theme.canvas
                            radius: 4
                            Layout.fillWidth: true
                            implicitHeight: code.implicitHeight + 20
                            Label {
                                id: code
                                anchors.fill: parent
                                anchors.margins: 10
                                text: "std::expected<T, E>\ncommit(transaction_id);\n12 reviews / 8 seconds"
                                textFormat: Text.PlainText
                                font.family: Theme.monoFont
                                color: Theme.ink
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }
            }
        }
    }
    footer: RowLayout {
        Item { Layout.fillWidth: true }
        AppButton { text: "Close"; onClicked: dialog.close() }
    }
}
