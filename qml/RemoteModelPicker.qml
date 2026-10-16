import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BetterFlash.Native 1.0 as Native

ColumnLayout {
    id: picker

    required property var binding
    required property string endpoint
    required property string category
    required property string modelName
    property string categoryLabel: "remote model"
    property string draftModelName: modelName
    property bool restoringEditor: false
    property string lastCommittedModel: modelName
    readonly property bool dialogOpen: modelEditor.popup.visible
    readonly property bool editorActiveFocus: modelEditor.activeFocus
        || modelEditor.popup.visible
        || modelEditor.contentItem && modelEditor.contentItem.activeFocus

    signal modelEdited(string model)

    spacing: 7
    Layout.fillWidth: true

    function restoreEditorDraft() {
        if (!modelEditor || modelEditor.editText === draftModelName)
            return
        restoringEditor = true
        modelEditor.editText = draftModelName
        restoringEditor = false
    }

    function commitEditorText() {
        if (restoringEditor)
            return
        const entered = String(modelEditor.editText || "").trim()
        if (entered.length === 0 || entered === lastCommittedModel)
            return
        draftModelName = entered
        lastCommittedModel = entered
        modelEdited(entered)
    }

    Native.RemoteModelCatalog {
        id: catalog
        apiKey: picker.binding
        endpoint: picker.endpoint
        category: picker.category
        active: picker.visible && picker.enabled
    }

    Label {
        text: "Remote model"
        color: Theme.inkMuted
        font.pixelSize: 15
        Layout.fillWidth: true
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 10

        AppComboBox {
            id: modelEditor
            Layout.fillWidth: true
            editable: true
            model: catalog.models
            font.pixelSize: 17
            comboHint: "Choose a fetched model or type a provider model identifier"

            Binding {
                target: modelEditor
                property: "editText"
                value: picker.draftModelName
                when: !modelEditor.activeFocus
            }

            onModelChanged: Qt.callLater(picker.restoreEditorDraft)

            onActivated: index => {
                if (index < 0 || index >= model.length)
                    return
                const selected = String(model[index]).trim()
                if (selected.length > 0) {
                    picker.draftModelName = selected
                    picker.lastCommittedModel = selected
                    picker.modelEdited(selected)
                }
            }
            onAccepted: {
                picker.commitEditorText()
            }
        }

        Connections {
            target: modelEditor.contentItem
            function onTextEdited() {
                if (!picker.restoringEditor)
                    picker.draftModelName = String(modelEditor.editText || "")
            }
            function onEditingFinished() {
                picker.commitEditorText()
            }
        }

        ModelActionButton {
            actionKind: "download"
            text: catalog.busy ? "Cancel fetch" : "Fetch models"
            enabled: picker.enabled && picker.binding !== null && picker.binding !== undefined
            hint: catalog.busy ? "Cancel the model list request"
                : catalog.categoryHint.length > 0 ? catalog.categoryHint
                : "Fetch available models for this remote endpoint"
            onClicked: catalog.busy ? catalog.cancel() : catalog.fetch()
        }
    }

    Label {
        visible: catalog.error.length > 0 || catalog.status.length > 0
        text: catalog.error.length > 0 ? catalog.error
            : catalog.status.length > 0 ? catalog.status : ""
        color: catalog.error.length > 0 ? Theme.error : Theme.inkMuted
        font.pixelSize: 14
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }

    onVisibleChanged: if (!visible && modelEditor.popup.visible) modelEditor.popup.close()

    onModelNameChanged: if (!modelEditor.activeFocus) {
        draftModelName = modelName
        lastCommittedModel = modelName
        restoreEditorDraft()
    }
}
