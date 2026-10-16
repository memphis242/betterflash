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
    readonly property bool dialogOpen: status.dialogOpen || keyPicker.visible
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

    function selectedChoice() {
        const choices = editor.binding ? (editor.binding.keyChoices || []) : []
        for (let i = 0; i < choices.length; ++i) {
            if (choices[i].id === editor.binding.selectedKeyId)
                return choices[i]
        }
        return null
    }

    function choiceName(choice) {
        if (!choice)
            return "New " + editor.credentialLabel.toLowerCase()
        if (choice.source && choice.source.length > 0)
            return choice.source + "-api-key"
        return choice.name || "Saved API key"
    }

    function choiceTitle(choice) {
        if (choice && editor.binding && choice.id === editor.binding.ownKeyId)
            return "This feature's key / enter a new " + editor.credentialLabel.toLowerCase()
        return editor.choiceName(choice)
    }

    function repositionKeyPicker() {
        if (!Overlay.overlay)
            return
        const fieldTopLeft = secretField.mapToItem(Overlay.overlay, 0, 0)
        const margin = 12
        keyPicker.width = Math.min(secretField.width, Math.max(180, Overlay.overlay.width - 2 * margin))
        keyPicker.x = Math.max(margin, Math.min(fieldTopLeft.x, Overlay.overlay.width - keyPicker.width - margin))
        const below = fieldTopLeft.y + secretField.height + 4
        const above = fieldTopLeft.y - keyPicker.height - 4
        keyPicker.y = below + keyPicker.height <= Overlay.overlay.height - margin
            ? below
            : Math.max(margin, above)
    }

    function openKeyPicker(fromKeyboard) {
        if (!editor.binding)
            return
        keyPicker.open()
        Qt.callLater(editor.repositionKeyPicker)
        keyPickerList.currentIndex = Math.max(0, editor.selectedIndex())
        if (fromKeyboard)
            keyPickerList.forceActiveFocus()
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

    RowLayout {
        Layout.fillWidth: true
        spacing: 10
        Label {
            text: editor.credentialLabel
            textFormat: Text.PlainText
            color: Theme.inkMuted
            font.family: Theme.uiFont
            font.pixelSize: 15
            Layout.fillWidth: true
        }
        AppButton {
            id: sourceButton
            text: "Reuse saved key"
            glyph: "chevron"
            hint: "Choose a saved key or enter a new key for this service"
            foreground: Theme.ink
            fill: Theme.transparent
            hoverFill: Theme.accentSoft
            outline: Theme.rule
            font.pixelSize: 15
            onClicked: editor.openKeyPicker(true)
        }
    }

    TextField {
        id: secretField
        Layout.fillWidth: true
        echoMode: TextInput.Password
        placeholderText: editor.binding && editor.binding.hasKey
            ? "Using " + editor.binding.keyName + " - type a new key for this feature"
            : "Paste a new " + editor.credentialLabel.toLowerCase()
        font.pixelSize: 17
        selectByMouse: true
        onVisibleChanged: if (!visible) text = ""
        onTextChanged: if (activeFocus && keyPicker.visible)
            keyPicker.close()
        TapHandler {
            onTapped: editor.openKeyPicker(false)
        }
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Down) {
                if (!keyPicker.visible)
                    editor.openKeyPicker(true)
                else
                    keyPickerList.forceActiveFocus()
                event.accepted = true
            } else if (event.key === Qt.Key_Escape && keyPicker.visible) {
                keyPicker.close()
                event.accepted = true
            }
        }
    }

    Popup {
        id: keyPicker
        parent: Overlay.overlay
        modal: false
        focus: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: 6
        onClosed: keyPickerList.currentIndex = -1

        background: Rectangle {
            color: Theme.comboPopupFill
            border.color: Theme.rule
            border.width: 1
            radius: 6
        }

        contentItem: ListView {
            id: keyPickerList
            implicitHeight: Math.min(contentHeight, 290)
            clip: true
            spacing: 3
            boundsBehavior: Flickable.StopAtBounds
            model: editor.binding ? (editor.binding.keyChoices || []) : []
            keyNavigationEnabled: true
            highlightFollowsCurrentItem: true
            delegate: Item {
                id: keyRow
                required property var modelData
                required property int index
                readonly property var choice: modelData
                width: keyPickerList.width
                implicitHeight: 58

                Rectangle {
                    anchors.fill: parent
                    radius: 4
                    color: keyRow.choice.selected || keyMouse.containsMouse || keyPickerList.currentIndex === keyRow.index
                        ? Theme.comboOptionHover : Theme.comboPopupFill
                    border.width: keyRow.choice.selected || keyPickerList.currentIndex === keyRow.index ? 1 : 0
                    border.color: Theme.accent
                }

                Column {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 12
                    anchors.topMargin: 7
                    anchors.bottomMargin: 6
                    spacing: 2

                    Label {
                        width: parent.width
                        text: editor.choiceTitle(keyRow.choice)
                        textFormat: Text.PlainText
                        color: Theme.ink
                        font.family: Theme.uiFont
                        font.pixelSize: 15
                        font.weight: keyRow.choice.selected ? Font.DemiBold : Font.Normal
                        elide: Text.ElideRight
                    }
                    Label {
                        width: parent.width
                        text: (keyRow.choice.sourcePage || keyRow.choice.name || "Saved key")
                            + (keyRow.choice.origin ? " • " + keyRow.choice.origin : "")
                            + (keyRow.choice.available === false
                                && !(editor.binding && keyRow.choice.id === editor.binding.ownKeyId)
                                ? " • Unavailable" : "")
                        textFormat: Text.PlainText
                        color: keyRow.choice.available === false
                            && !(editor.binding && keyRow.choice.id === editor.binding.ownKeyId)
                            ? Theme.warning : Theme.inkMuted
                        font.family: Theme.contentFont
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }
                }

                MouseArea {
                    id: keyMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        if (editor.binding && editor.binding.selectKey(keyRow.choice.id)) {
                            editor.clearSecretDraft()
                            editor.transientError = ""
                            keyPicker.close()
                            secretField.forceActiveFocus()
                        } else if (editor.binding) {
                            editor.transientError = editor.binding.error || "KEY_SOURCE_REJECTED: Choose a compatible saved key."
                        }
                    }
                }
            }
            Keys.onPressed: event => {
                if (event.key === Qt.Key_Escape) {
                    keyPicker.close()
                    secretField.forceActiveFocus()
                    event.accepted = true
                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) {
                    const choices = editor.binding ? (editor.binding.keyChoices || []) : []
                    if (currentIndex >= 0 && currentIndex < choices.length) {
                        if (editor.binding.selectKey(choices[currentIndex].id)) {
                            editor.clearSecretDraft()
                            editor.transientError = ""
                            keyPicker.close()
                            secretField.forceActiveFocus()
                        }
                    }
                    event.accepted = true
                }
            }
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 8

        Flow {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 10
            AppButton {
                text: editor.saveAction
                hint: "Save a new key for this feature without changing the selected shared key"
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
                visible: !!editor.binding && !editor.binding.reusingKey
                    && (editor.binding.hasKey || editor.binding.storageState === "removal-failed")
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
            if (!editor.binding || !editor.binding.error)
                editor.transientError = ""
        }
    }

    onSelectionIdentityChanged: clearSecretDraft()
    onBindingChanged: {
        clearSecretDraft()
        if (keyPicker.visible)
            keyPicker.close()
    }
    onVisibleChanged: if (!visible) {
        clearSecretDraft()
        if (keyPicker.visible)
            keyPicker.close()
    }
}
