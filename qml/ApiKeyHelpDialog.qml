import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog

    property string credentialLabel: "API key"
    property bool reusesLanguageModelKey: false
    property string saveAction: "Save key"
    property string clearAction: "Clear stored key"
    readonly property string credentialNoun: credentialLabel === "API key" ? credentialLabel : credentialLabel.toLowerCase()

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(640, parent ? parent.width - 32 : 640)
    height: Math.min(560, parent ? parent.height - 32 : 560)
    padding: 20
    modal: true
    closePolicy: Popup.CloseOnEscape

    background: Rectangle {
        color: Theme.surface
        radius: 14
        border.color: Theme.rule
        border.width: 1
    }
    Overlay.modal: Rectangle { color: Theme.scrim }

    function openFor() {
        open()
        closeButton.forceActiveFocus()
    }

    header: Item {
        implicitHeight: 54
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 4
            anchors.rightMargin: 4
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: "About " + dialog.credentialLabel + " storage"
                color: Theme.ink
                font.family: Theme.uiFont
                font.pixelSize: 20
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
            }
            GlyphButton {
                id: closeButton
                glyph: "close"
                hint: "Close credential storage help"
                onClicked: dialog.close()
            }
        }
    }

    contentItem: ScrollView {
        id: viewport
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            width: viewport.availableWidth
            spacing: 16

            Label {
                Layout.fillWidth: true
                visible: !dialog.reusesLanguageModelKey
                text: "Paste the " + dialog.credentialNoun + " into the field, then choose " + dialog.saveAction + ". BetterFlash stores it in the operating system's secure keyring and retrieves it when the application starts again."
                color: Theme.ink
                font.family: Theme.contentFont
                font.pixelSize: 16
                wrapMode: Text.Wrap
            }
            Label {
                Layout.fillWidth: true
                visible: dialog.reusesLanguageModelKey
                text: "This service reuses the credential configured in Language model settings. The connection choice here only selects whether this service uses that credential; it does not provide a second credential field or manage another key."
                color: Theme.ink
                font.family: Theme.contentFont
                font.pixelSize: 16
                wrapMode: Text.Wrap
            }
            Rectangle {
                visible: !dialog.reusesLanguageModelKey
                Layout.fillWidth: true
                implicitHeight: storageSteps.implicitHeight + 24
                color: Theme.editorField
                border.color: Theme.rule
                radius: 8
                ColumnLayout {
                    id: storageSteps
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 7
                    Label { Layout.fillWidth: true; text: "1. Enter the credential in the masked field."; color: Theme.ink; font.pixelSize: 15; wrapMode: Text.Wrap }
                    Label { Layout.fillWidth: true; text: "2. " + dialog.saveAction + " writes it to the secure keyring."; color: Theme.ink; font.pixelSize: 15; wrapMode: Text.Wrap }
                    Label { Layout.fillWidth: true; text: "3. " + dialog.clearAction + " removes the saved copy from the keyring."; color: Theme.ink; font.pixelSize: 15; wrapMode: Text.Wrap }
                }
            }
            Label {
                Layout.fillWidth: true
                visible: !dialog.reusesLanguageModelKey
                text: "Leaving the field blank is normal when a credential is already saved. The field stays blank so the secret is not exposed in the interface. A saved credential is used only by the service it belongs to."
                color: Theme.ink
                font.pixelSize: 16
                wrapMode: Text.Wrap
            }
            Label {
                Layout.fillWidth: true
                visible: dialog.reusesLanguageModelKey
                text: "To change or clear the shared credential, use the Language model settings page. The credential remains associated with the language model service and is not copied for this service."
                color: Theme.info
                font.pixelSize: 16
                wrapMode: Text.Wrap
            }
            Label {
                Layout.fillWidth: true
                text: dialog.reusesLanguageModelKey
                    ? "If the secure keyring or its integration is unavailable, the shared credential may remain available only until BetterFlash closes. Manage it from the Language model settings page."
                    : "If the secure keyring or its integration is unavailable, the credential may remain available only until BetterFlash closes. An environment-supplied credential is read from the configured environment variable instead of being saved by this page."
                color: Theme.inkMuted
                font.pixelSize: 15
                wrapMode: Text.Wrap
            }
        }
    }
}
