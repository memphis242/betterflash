import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: control

    property string storageState: "missing"
    property string storageStatus: ""
    property bool hasKey: false
    property bool reusesLanguageModelKey: false
    property bool reusingKey: false
    property string keyName: ""
    property string sourcePage: ""
    property bool showProviderScope: false
    property string credentialLabel: "API key"
    property string saveAction: "Save key"
    property string clearAction: "Clear stored key"
    readonly property string credentialNoun: credentialLabel === "API key" ? credentialLabel : credentialLabel.toLowerCase()
    readonly property bool dialogOpen: helpDialog.visible

    implicitWidth: 290
    spacing: 7

    function stateLabel() {
        let status = ""
        switch (control.storageState) {
        case "saved":
        case "removed":
            status = control.storageState === "saved" ? "Saved on this computer" : "No " + control.credentialNoun + " saved"
            break
        case "loading":
            status = "Checking saved key..."
            break
        case "saving":
            status = "Saving securely..."
            break
        case "removing":
            status = "Removing saved key..."
            break
        case "environment":
            status = "Provided by environment"
            break
        case "session-only":
            status = "Not saved. Available until BetterFlash closes."
            break
        case "removal-failed":
            status = "Saved copy may remain"
            break
        case "invalid":
            status = "New key was rejected"
            break
        case "unavailable":
            status = "Selected key unavailable"
            break
        case "missing":
        default:
            status = "No " + control.credentialNoun + " saved"
            break
        }
        return (control.reusingKey && control.keyName.length > 0 ? "Using " + control.keyName + " - " : "")
            + (control.reusesLanguageModelKey ? "Language model key: " : "") + status
    }

    function stateColor() {
        switch (control.storageState) {
        case "saved": return Theme.success
        case "saving":
        case "removing":
        case "loading":
        case "environment": return Theme.info
        case "session-only": return Theme.warning
        case "removal-failed":
        case "invalid": return Theme.error
        case "unavailable": return Theme.error
        default: return Theme.inkMuted
        }
    }

    function attentionText() {
        switch (control.storageState) {
        case "session-only":
            return control.reusingKey || control.reusesLanguageModelKey
                ? "CREDENTIAL_SESSION_ONLY: The shared credential could not be saved by the secure keyring or its integration. Make sure a desktop keyring service is available, then save it again in " + (control.sourcePage || "its source settings") + "."
                : "CREDENTIAL_SESSION_ONLY: The secure keyring or its integration is unavailable. Make sure a desktop keyring service is available, then enter the credential again and choose " + control.saveAction + "."
        case "removal-failed":
            return control.reusingKey || control.reusesLanguageModelKey
                ? "CREDENTIAL_REMOVE_FAILED: The shared credential is cleared for this session, but its saved copy may still be in the desktop keyring. Remove it again from " + (control.sourcePage || "its source settings") + " after the keyring is available."
                : "CREDENTIAL_REMOVE_FAILED: The credential is cleared for this session, but its saved copy may still be in the desktop keyring. Try " + control.clearAction + " again after the keyring is available."
        case "invalid":
            return control.reusingKey || control.reusesLanguageModelKey
                ? "CREDENTIAL_INVALID: The new shared credential was rejected. Enter one credential of at most 4096 characters without line breaks in " + (control.sourcePage || "its source settings") + ". The previous credential, if any, remains unchanged."
                : "CREDENTIAL_INVALID: Enter one credential of at most 4096 characters without line breaks. The previously usable credential, if any, remains unchanged."
        case "unavailable":
            return "KEY_SELECTION: The selected credential is unavailable for this provider address. Choose a compatible key from the source list, or switch to the key owned by this page."
        default:
            return ""
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Rectangle {
            implicitWidth: 10
            implicitHeight: 10
            radius: 5
            color: Theme.transparent
            border.width: 2
            border.color: control.stateColor()
            Layout.alignment: Qt.AlignVCenter
        }
        Label {
            Layout.fillWidth: true
            text: control.stateLabel()
            color: control.stateColor()
            font.family: Theme.uiFont
            font.pixelSize: 14
            wrapMode: Text.Wrap
            Accessible.name: control.credentialLabel + " storage status: " + text
        }
        HelpButton {
            id: helpButton
            label: control.credentialLabel + " storage"
            hint: "Learn about " + control.credentialNoun + " storage"
            onClicked: helpDialog.openFor()
            Layout.alignment: Qt.AlignVCenter
        }
    }

    Rectangle {
        visible: control.attentionText().length > 0
        Layout.fillWidth: true
        implicitHeight: attentionLabel.implicitHeight + 20
        color: control.storageState === "session-only"
            ? Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.14)
            : Qt.rgba(Theme.error.r, Theme.error.g, Theme.error.b, 0.14)
        border.color: control.storageState === "session-only" ? Theme.warning : Theme.error
        border.width: 1
        radius: 7
        Label {
            id: attentionLabel
            anchors.fill: parent
            anchors.margins: 10
            text: control.attentionText()
            color: Theme.ink
            font.family: Theme.contentFont
            font.pixelSize: 14
            wrapMode: Text.Wrap
        }
    }

    ApiKeyHelpDialog {
        id: helpDialog
        credentialLabel: control.credentialLabel
        reusesLanguageModelKey: control.reusesLanguageModelKey
        reusingKey: control.reusingKey
        keyName: control.keyName
        sourcePage: control.sourcePage
        showProviderScope: control.showProviderScope
        saveAction: control.saveAction
        clearAction: control.clearAction
    }

    onVisibleChanged: {
        if (!visible && helpDialog.visible)
            helpDialog.close()
    }
}
