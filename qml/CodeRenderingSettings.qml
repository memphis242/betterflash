import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: settings
    spacing: 18
    component SettingsButton: AppButton { font.pixelSize: 16 }
    component SettingsCombo: ComboBox { font.pixelSize: 17 }
    component SettingsSwitch: AppSwitch { font.pixelSize: 16 }
    component SettingsError: ErrorBlock { textSize: 16 }

    property string pathError: ""

    Label {
        Layout.fillWidth: true
        text: "Fenced Markdown blocks use the language after the opening backticks. Choose separate code colors for each app appearance."
        color: Theme.inkMuted
        wrapMode: Text.Wrap
        font.pixelSize: 16
    }
    Label {
        text: "Dark appearance"
        color: Theme.inkMuted
        font.pixelSize: 15
    }
    SettingsCombo {
        Layout.fillWidth: true
        Layout.maximumWidth: 400
        model: codeRendering.darkThemes
        currentIndex: codeRendering.darkThemes.indexOf(codeRendering.darkTheme)
        onActivated: codeRendering.darkTheme = currentText
        ToolTip.visible: hovered
        ToolTip.text: "Code theme when the app uses dark mode"
    }
    Label {
        text: "Light appearance"
        color: Theme.inkMuted
        font.pixelSize: 15
    }
    SettingsCombo {
        Layout.fillWidth: true
        Layout.maximumWidth: 400
        model: codeRendering.lightThemes
        currentIndex: codeRendering.lightThemes.indexOf(codeRendering.lightTheme)
        onActivated: codeRendering.lightTheme = currentText
        ToolTip.visible: hovered
        ToolTip.text: "Code theme when the app uses light mode"
    }
    MarkdownPane {
        Layout.fillWidth: true
        baseFontSize: 17
        markdown: "```cpp\n#include <expected>\n#include <string>\n\n// A value, or an explanation of the failure.\nstd::expected<int, std::string> divide(int value) {\n    if (value == 0) return std::unexpected(\"Zero divisor\");\n    return 42 / value;\n}\n```"
    }
    SettingsSwitch {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        text: "Use language server highlighting"
        checked: codeRendering.languageServerEnabled
        onToggled: codeRendering.languageServerEnabled = checked
        ToolTip.visible: hovered
        ToolTip.text: "Add C and C++ semantic colors from a local clangd process"
    }
    Label {
        Layout.fillWidth: true
        text: "Native syntax highlighting covers recognized languages immediately. C and C++ additionally use clangd when available. Snippets are parsed in an isolated temporary workspace; they are never built or run. Android uses syntax highlighting."
        color: Theme.inkMuted
        wrapMode: Text.Wrap
        font.pixelSize: 16
    }
    Label {
        Layout.fillWidth: true
        text: codeRendering.serverStatus
        color: text.indexOf("CODE_") === 0 ? Theme.warning : Theme.inkMuted
        wrapMode: Text.Wrap
        font.pixelSize: 16
    }
    Label {
        text: "Language server executable"
        color: Theme.inkMuted
        font.pixelSize: 15
    }
    TextField {
        id: serverPath
        Layout.fillWidth: true
        text: codeRendering.clangdPath
        placeholderText: "Automatic from PATH"
        selectByMouse: true
        font.pixelSize: 17
    }
    Label {
        Layout.fillWidth: true
        text: "Choose an executable path, without command-line arguments. Saving retries a stopped language server."
        color: Theme.inkMuted
        wrapMode: Text.Wrap
        font.pixelSize: 16
    }
    SettingsButton {
        text: "Save language server"
        primary: true
        onClicked: {
            if (!codeRendering.setClangdPath(serverPath.text))
                settings.pathError = "CODE_SERVER_PATH: Choose an executable clangd file, or install clangd and leave the path empty.";
            else
                settings.pathError = "";
        }
    }
    SettingsError {
        Layout.fillWidth: true
        message: settings.pathError
    }
}
