import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import BetterFlash.Native

ColumnLayout {
    id: root

    property string text: ""
    property bool selectDirectory: false
    property var nameFilters: ["Model files (*.bin)", "All files (*)"]
    property string label: "Model path"
    property string title: selectDirectory ? "Choose model folder" : "Choose model file"
    property string error: ""
    readonly property bool dialogOpen: fileDialog.visible || folderDialog.visible
    signal pathEdited(string path)

    spacing: 5
    Layout.fillWidth: true

    onTextChanged: {
        if (!pathField.activeFocus && !root.dialogOpen)
            pathField.text = root.text
    }
    onDialogOpenChanged: {
        if (!root.dialogOpen && !pathField.activeFocus)
            pathField.text = root.text
    }

    Label {
        text: root.label
        color: Theme.inkMuted
        font.pixelSize: 15
        Layout.fillWidth: true
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        TextField {
            id: pathField
            text: root.text
            Layout.fillWidth: true
            selectByMouse: true
            font.pixelSize: 17
            placeholderText: root.label
            onEditingFinished: root.pathEdited(text)
        }

        GlyphButton {
            glyph: "folder"
            glyphSize: 19
            hint: root.selectDirectory ? "Choose a model folder" : "Choose a model file"
            Accessible.name: hint
            onClicked: root.openPicker()
        }
    }

    Label {
        visible: root.error.length > 0
        text: root.error
        color: Theme.error
        wrapMode: Text.Wrap
        Layout.fillWidth: true
        font.pixelSize: 14
    }

    function openPicker() {
        root.error = ""
        const initialPath = LocalFilePaths.initialFolder(root.text)
        const initialUrl = LocalFilePaths.fromLocalPath(initialPath)
        if (root.selectDirectory) {
            folderDialog.currentFolder = initialUrl
            folderDialog.open()
        } else {
            fileDialog.currentFolder = initialUrl
            fileDialog.open()
        }
    }

    FileDialog {
        id: fileDialog
        title: root.title
        fileMode: FileDialog.OpenFile
        nameFilters: root.nameFilters
        onAccepted: root.acceptSelection(selectedFile)
    }

    FolderDialog {
        id: folderDialog
        title: root.title
        onAccepted: root.acceptSelection(selectedFolder)
    }

    function acceptSelection(url) {
        if (!LocalFilePaths.isLocalUrl(url)) {
            root.error = "PATH_SELECTION: Choose a local file or folder on this device."
            return
        }
        const selectedPath = LocalFilePaths.toLocalPath(url)
        if (!selectedPath) {
            root.error = "PATH_SELECTION: The selected path could not be read. Choose it again."
            return
        }
        if (!LocalFilePaths.isReadableSelection(selectedPath, root.selectDirectory)) {
            root.error = root.selectDirectory
                ? "PATH_SELECTION: Choose a readable local folder."
                : "PATH_SELECTION: Choose a readable local model file."
            return
        }
        root.error = ""
        root.pathEdited(selectedPath)
    }
}
