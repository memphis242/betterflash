import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: editor

    required property var binding
    property string credentialLabel: "API key"
    property string saveAction: "Save key"
    property string clearAction: "Clear stored key"
    property string transientError: ""
    readonly property bool dialogOpen: status.dialogOpen || sourceCombo.popup.visible
    readonly property bool usingOwnKey: !binding || !binding.reusingKey
    readonly property string selectionIdentity: binding
        ? String(binding.ownKeyId) + "|" + String(binding.selectedKeyId)
        : ""

    implicitWidth: 420
    spacing: 10

    function clearSecretDraft() {
        if (secretField)
            secretField.text = ""
    }

    function selectedIndex() {
        if (!editor.binding)
            return -1
        const choices = editor.binding.keyChoices || []
        for (let i = 0; i < choices.length; ++i) {
            if (choices[i].id === editor.binding.selectedKeyId)
                return i
        }
        return -1
    }

    Label {
        text: "Key source"
        textFormat: Text.PlainText
        color: Theme.inkMuted
        font.family: Theme.uiFont
        font.pixelSize: 15
        Layout.fillWidth: true
    }

    AppComboBox {
        id: sourceCombo
        Layout.fillWidth: true
        Layout.minimumWidth: 260
        model: editor.binding ? (editor.binding.keyChoices || []) : []
        textRole: "label"
        valueRole: "id"
        currentIndex: editor.selectedIndex()
        comboHint: "Choose a saved key or enter a new key for this service"
        onActivated: index => {
            if (!editor.binding || index < 0 || index >= model.length)
                return
            const choice = model[index]
            if (!editor.binding.selectKey(choice.id)) {
                sourceCombo.currentIndex = editor.selectedIndex()
                editor.transientError = editor.binding.error || "KEY_SOURCE_REJECTED: Choose a compatible saved key."
            } else {
                editor.transientError = ""
                editor.clearSecretDraft()
            }
        }
    }

    Label {
        visible: editor.binding && editor.binding.reusingKey
        textFormat: Text.PlainText
        text: editor.binding && editor.binding.sourcePage.length > 0
            ? "Managed from " + editor.binding.sourcePage + ". Changes made there apply here automatically."
            : "This service uses the selected saved key. Changes made at its source apply here automatically."
        color: Theme.info
        font.family: Theme.contentFont
        font.pixelSize: 14
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }

    ColumnLayout {
        visible: editor.usingOwnKey
        Layout.fillWidth: true
        spacing: 8

        Label {
            text: "Enter " + editor.credentialLabel
            textFormat: Text.PlainText
            color: Theme.inkMuted
            font.family: Theme.uiFont
            font.pixelSize: 15
            Layout.fillWidth: true
        }
        TextField {
            id: secretField
            Layout.fillWidth: true
            echoMode: TextInput.Password
            placeholderText: editor.binding && editor.binding.hasKey
                ? "Leave empty to keep the saved " + editor.credentialLabel.toLowerCase()
                : editor.credentialLabel
            font.pixelSize: 17
            onVisibleChanged: if (!visible) text = ""
        }
        Flow {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 10
            AppButton {
                text: editor.saveAction
                enabled: !!editor.binding && secretField.text.trim().length > 0
                onClicked: {
                    if (editor.binding.saveKey(secretField.text.trim())) {
                        editor.clearSecretDraft()
                        editor.transientError = ""
                    } else {
                        editor.transientError = editor.binding.error || "KEY_SAVE_FAILED: The credential could not be saved."
                    }
                }
            }
            AppButton {
                text: editor.clearAction
                visible: !!editor.binding && (editor.binding.hasKey || editor.binding.storageState === "removal-failed")
                onClicked: {
                    if (!editor.binding.clearKey())
                        editor.transientError = editor.binding.error || "KEY_CLEAR_FAILED: The saved credential could not be removed."
                    else
                        editor.clearSecretDraft()
                }
            }
        }
    }

    ApiKeyStatus {
        id: status
        Layout.fillWidth: true
        storageState: editor.binding ? editor.binding.storageState : "loading"
        storageStatus: editor.binding ? editor.binding.storageStatus : ""
        hasKey: editor.binding ? editor.binding.hasKey : false
        reusingKey: editor.binding ? editor.binding.reusingKey : false
        keyName: editor.binding ? editor.binding.keyName : ""
        sourcePage: editor.binding ? editor.binding.sourcePage : ""
        showProviderScope: true
        credentialLabel: editor.credentialLabel
        saveAction: editor.saveAction
        clearAction: editor.clearAction
    }

    Label {
        id: errorLabel
        visible: text.length > 0
        text: editor.binding && editor.binding.error ? editor.binding.error : editor.transientError
        textFormat: Text.PlainText
        color: Theme.error
        font.family: Theme.contentFont
        font.pixelSize: 14
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }

    Connections {
        target: editor.binding
        function onChanged() {
            sourceCombo.currentIndex = editor.selectedIndex()
            if (!editor.binding || !editor.binding.error)
                editor.transientError = ""
        }
    }

    onSelectionIdentityChanged: clearSecretDraft()
    onBindingChanged: {
        if (sourceCombo)
            sourceCombo.currentIndex = selectedIndex()
        clearSecretDraft()
    }
    onVisibleChanged: if (!visible) clearSecretDraft()
}
