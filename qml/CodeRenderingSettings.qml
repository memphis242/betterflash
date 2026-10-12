import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: settings
    spacing: 18
    property string pathError: ""

    Label {
        Layout.fillWidth: true
        text: "Fenced Markdown blocks use the language after the opening backticks. Choose separate code colors for each app appearance."
        color: Theme.inkMuted
        wrapMode: Text.Wrap
    }
    Label {
        text: "Dark appearance"
        color: Theme.inkMuted
    }
    ComboBox {
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
    }
    ComboBox {
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
    AppSwitch {
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
    }
    Label {
        Layout.fillWidth: true
        text: codeRendering.serverStatus
        color: text.indexOf("CODE_") === 0 ? Theme.warning : Theme.inkMuted
        wrapMode: Text.Wrap
        font.pixelSize: 13
    }
    AppButton {
        Layout.maximumWidth: settings.width
        text: "Configure language server"
        onClicked: serverDialog.open()
    }
    Dialog {
        id: serverDialog
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(580, parent ? parent.width - 40 : 580)
        title: "C and C++ language server"
        header: Label {
            text: serverDialog.title
            color: Theme.ink
            font.pixelSize: 20
            leftPadding: 20
            topPadding: 20
            bottomPadding: 12
            rightPadding: 20
        }
        background: Rectangle {
            color: Theme.surface
            radius: 18
            border.color: Theme.rule
        }
        onOpened: {
            serverPath.text = codeRendering.clangdPath;
            settings.pathError = "";
        }
        contentItem: ColumnLayout {
            spacing: 14
            Label {
                text: "clangd executable"
                color: Theme.inkMuted
            }
            TextField {
                id: serverPath
                Layout.fillWidth: true
                placeholderText: "Automatic from PATH"
                selectByMouse: true
            }
            Label {
                Layout.fillWidth: true
                text: "Choose an executable path, without command-line arguments. Save also retries a stopped language server."
                color: Theme.inkMuted
                wrapMode: Text.Wrap
            }
            ErrorBlock {
                Layout.fillWidth: true
                message: settings.pathError
            }
        }
        footer: RowLayout {
            spacing: 12
            Item {
                Layout.fillWidth: true
            }
            AppButton {
                text: "Cancel"
                onClicked: serverDialog.close()
            }
            AppButton {
                text: "Save"
                primary: true
                onClicked: {
                    if (codeRendering.setClangdPath(serverPath.text))
                        serverDialog.close();
                    else
                        settings.pathError = "CODE_SERVER_PATH: Choose an executable clangd file, or install clangd and leave the path empty.";
                }
            }
        }
    }
}
