import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog

    property string slotId: ""
    property string slotLabel: ""
    property bool readOnly: false
    property string saveError: ""
    signal saved(string id, string text)

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(720, parent ? parent.width - 2 * 24 : 720)
    height: Math.min(520, parent ? parent.height - 2 * 24 : 520)
    padding: 20
    modal: true
    closePolicy: Popup.CloseOnEscape
    background: Rectangle { color: Theme.surface; radius: 14; border.color: Theme.ledgerRule }
    Overlay.modal: Rectangle { color: Theme.scrim }

    function openFor(id, label, value) {
        slotId = String(id)
        slotLabel = String(label)
        saveError = ""
        editor.text = String(value)
        open()
    }

    function save() {
        if (dialog.readOnly || !dialog.slotId) return
        dialog.saved(dialog.slotId, editor.text)
    }

    function showSaveError(message) {
        saveError = String(message)
    }

    header: Item {
        implicitHeight: 64
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            anchors.topMargin: 8
            anchors.bottomMargin: 8
            Label {
                text: "Expected transcription"
                color: Theme.ink
                font.family: Theme.uiFont
                font.pixelSize: 21
                font.bold: true
                Layout.fillWidth: true
            }
            GlyphButton { glyph: "close"; hint: "Close expected transcription editor"; onClicked: dialog.close() }
        }
    }

    contentItem: ColumnLayout {
        spacing: 10
        Label {
            Layout.fillWidth: true
            text: dialog.slotLabel ? dialog.slotLabel + " - edit the words this recording should produce" : "Edit the words this recording should produce"
            color: Theme.inkMuted
            wrapMode: Text.Wrap
            font.pixelSize: 13
        }
        ScrollView {
            id: editorViewport
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical.policy: ScrollBar.AsNeeded
            background: Rectangle {
                radius: 7
                color: dialog.readOnly ? Theme.canvas : editor.activeFocus ? Theme.surface : Theme.surfaceRaised
                border.color: editor.activeFocus && !dialog.readOnly ? Theme.accent : Theme.rule
                border.width: editor.activeFocus ? 2 : 1
            }
            TextArea {
                id: editor
                width: editorViewport.availableWidth
                implicitWidth: 0
                height: Math.max(editorViewport.availableHeight, implicitHeight)
                readOnly: dialog.readOnly
                placeholderText: "Add expected transcription"
                placeholderTextColor: Theme.inkMuted
                color: dialog.readOnly ? Theme.inkMuted : Theme.ink
                font.family: Theme.uiFont
                font.pixelSize: 16
                wrapMode: TextEdit.Wrap
                selectByMouse: true
                background: Item {}
                Accessible.name: "Expected transcription editor for " + dialog.slotLabel
                Keys.onPressed: function(event) {
                    if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && event.modifiers & Qt.ControlModifier) {
                        dialog.save()
                        event.accepted = true
                    }
                }
            }
        }
        Label {
            visible: dialog.readOnly
            Layout.fillWidth: true
            text: "Expected transcription is locked while recording or testing."
            color: Theme.warning
            font.pixelSize: 12
            wrapMode: Text.Wrap
        }
        Label {
            visible: dialog.saveError.length > 0
            Layout.fillWidth: true
            text: dialog.saveError
            color: Theme.error
            font.pixelSize: 12
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            AppButton { text: "Cancel"; onClicked: dialog.close() }
            AppButton {
                text: "Save expected transcription"
                primary: true
                enabled: !dialog.readOnly
                onClicked: dialog.save()
            }
        }
    }

    onOpened: if (!dialog.readOnly) editor.forceActiveFocus()
}
