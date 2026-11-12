import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: settings
    required property var service
    readonly property var levels: ["error", "warning", "info", "debug"]
    readonly property var verbosityLevels: ["low", "normal", "high"]
    spacing: 14

    Label {
        text: "Keep local diagnostic logs to investigate application and model failures. Changes apply immediately."
        textFormat: Text.PlainText
        color: Theme.ink
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }
    Label { text: "Log level"; color: Theme.ink; font.bold: true }
    AppComboBox {
        objectName: "diagnosticLogLevel"
        model: ["Error", "Warning", "Info", "Debug"]
        currentIndex: Math.max(0, settings.levels.indexOf(settings.service.level))
        onActivated: settings.service.level = settings.levels[currentIndex]
        comboHint: "Records this severity and higher. Warning is the default; Debug records more application activity."
        Accessible.name: "Diagnostic log level"
        Layout.fillWidth: true
    }
    Label { text: "Diagnostic verbosity"; color: Theme.ink; font.bold: true }
    AppComboBox {
        objectName: "diagnosticLogVerbosity"
        model: ["Low - event only", "Normal - metadata", "High - full diagnostic context"]
        currentIndex: Math.max(0, settings.verbosityLevels.indexOf(settings.service.verbosity))
        onActivated: settings.service.verbosity = settings.verbosityLevels[currentIndex]
        comboHint: "Controls the detail saved for each event, independently of its severity. High is the default."
        Accessible.name: "Diagnostic verbosity"
        Layout.fillWidth: true
    }
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: privacyNote.implicitHeight + 24
        color: Theme.surfaceRaised
        border.color: Theme.rule
        radius: 6
        Label {
            id: privacyNote
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 12
            text: "High verbosity saves card text, prompts, model replies, and search/tool exchanges when a model operation fails or misuses a tool. Debug level also records successful model calls. Logs can contain private collection content. Credentials are redacted. Lower verbosity reduces saved detail. Inspect logs before sharing them."
            textFormat: Text.PlainText
            color: Theme.ink
            wrapMode: Text.Wrap
        }
    }
    Label {
        text: "Logs rotate daily in UTC and when a file fills. Up to 16 files of 4 MiB are retained, with a 64 MiB total limit and seven-day retention. Oldest files are removed automatically. Logs stay on this device and are excluded from collection sync and exports."
        textFormat: Text.PlainText
        color: Theme.inkMuted
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }
    Label { text: "Log directory"; color: Theme.ink; font.bold: true }
    TextEdit {
        text: settings.service.logDirectory
        textFormat: TextEdit.PlainText
        readOnly: true
        selectByMouse: true
        wrapMode: TextEdit.WrapAnywhere
        color: Theme.ink
        font.family: Theme.monoFont
        Layout.fillWidth: true
        Accessible.name: "Diagnostic log directory"
    }
    AppButton {
        text: "Open log directory"
        hint: "Open local diagnostic files in your file manager. Review private content before sharing files."
        onClicked: settings.service.openLogDirectory()
    }
    Rectangle {
        visible: !!settings.service.errorInfo.message
        Layout.fillWidth: true
        implicitHeight: Math.min(errorBlock.implicitHeight, 220) + 24
        color: Theme.errorSurface
        border.color: Theme.error
        radius: 6
        ScrollView {
            anchors.fill: parent
            anchors.margins: 12
            contentWidth: availableWidth
            clip: true
            ErrorBlock {
                id: errorBlock
                width: parent.width
                message: settings.service.errorInfo.message || ""
                code: settings.service.errorInfo.code || ""
                details: settings.service.errorInfo.detail || ""
                textSize: 15
            }
        }
    }
}
