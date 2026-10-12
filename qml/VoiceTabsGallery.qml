import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    objectName: "voiceTabsGallery"
    property int selectedStyle: 0
    property var choices: [
        { number: "01", name: "Ledger tabs", description: "A clear folder-like tab edge with an active underline." },
        { number: "02", name: "Segmented selector", description: "A compact control for users who already know there are two modes." },
        { number: "03", name: "Voice rail", description: "Large icon-led tabs that make the two voice directions unmistakable." }
    ]
    width: 1040
    height: 800
    minimumWidth: 700
    minimumHeight: 560
    visible: true
    title: "BetterFlash - Voice settings tab studies"
    color: Theme.canvas
    font.family: Theme.uiFont
    font.pixelSize: 14
    palette.window: Theme.canvas
    palette.windowText: Theme.ink
    palette.text: Theme.ink
    palette.buttonText: Theme.ink
    palette.highlight: Theme.accentSoft
    palette.highlightedText: Theme.ink

    Rectangle {
        anchors.fill: parent
        color: Theme.canvas
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 30
        spacing: 18
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: "Voice settings tab studies"
                color: Theme.ink
                font.family: Theme.uiFont
                font.pixelSize: 26
                font.bold: true
                Layout.fillWidth: true
            }
            AppButton {
                glyph: Theme.dark ? "sun" : "moon"
                hint: Theme.dark ? "Switch to light theme" : "Switch to dark theme"
                onClicked: Theme.toggle()
            }
        }
        Label {
            text: "Choose a treatment that makes speech recognition and speech output immediately discoverable."
            color: Theme.inkMuted
            font.pixelSize: 16
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ColumnLayout {
                width: Math.max(1, root.width - 60)
                spacing: 16
                Repeater {
                    model: root.choices
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        implicitHeight: 182
                        color: Theme.surface
                        border.color: root.selectedStyle === index ? Theme.accent : Theme.rule
                        border.width: root.selectedStyle === index ? 2 : 1
                        radius: 9
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 16
                            spacing: 10
                            RowLayout {
                                Layout.fillWidth: true
                                Label {
                                    text: modelData.number + "  " + modelData.name
                                    color: Theme.ink
                                    font.family: Theme.uiFont
                                    font.pixelSize: 17
                                    font.bold: true
                                }
                                Item { Layout.fillWidth: true }
                                AppButton {
                                    text: root.selectedStyle === index ? "Previewing" : "Preview"
                                    primary: root.selectedStyle === index
                                    hint: "Highlight this tab treatment in the study gallery"
                                    onClicked: root.selectedStyle = index
                                }
                            }
                            Label {
                                text: modelData.description
                                color: Theme.inkMuted
                                font.pixelSize: 14
                            }
                            VoiceSettingsTabs {
                                id: previewTabs
                                Layout.fillWidth: true
                                styleIndex: index
                                property int previewIndex: 0
                                currentIndex: previewIndex
                                onTabSelected: previewIndex = index
                            }
                            Label {
                                Layout.fillWidth: true
                                text: previewTabs.previewIndex === 0
                                    ? "Recognition section: provider, local model, and microphone checks"
                                    : "Output section: voice profile, playback engine, and speech checks"
                                color: Theme.inkMuted
                                font.pixelSize: 14
                                wrapMode: Text.Wrap
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            z: -1
                            onClicked: root.selectedStyle = index
                        }
                    }
                }
            }
        }
    }
}
