import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    property string topic: ""
    readonly property string topicTitle: topic === "vocabulary" ? "Recognition vocabulary"
        : topic === "stt-model" ? "Selected local model (STT)"
        : topic === "stt-acceleration" ? "Speech recognition acceleration"
        : topic === "tts-engine" ? "Local speech engine (TTS)"
        : topic === "tts-profile" ? "Text-to-speech profile (TTS)" : "Voice setting"

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(780, parent ? parent.width - 32 : 780)
    height: Math.min(720, parent ? parent.height - 32 : 720)
    padding: 20
    modal: true
    closePolicy: Popup.CloseOnEscape
    background: Rectangle {
        color: Theme.surface
        radius: 14
        border.color: Theme.rule
    }
    Overlay.modal: Rectangle { color: Theme.scrim }

    function openTopic(value) {
        topic = String(value || "")
        open()
        closeButton.forceActiveFocus()
    }

    header: Item {
        implicitHeight: 58
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 4
            anchors.rightMargin: 4
            anchors.topMargin: 3
            anchors.bottomMargin: 3
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: dialog.topicTitle
                textFormat: Text.PlainText
                color: Theme.ink
                font.family: Theme.uiFont
                font.pixelSize: 21
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
            }
            GlyphButton {
                id: closeButton
                glyph: "close"
                hint: "Close voice setting help"
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
            spacing: 18

            ColumnLayout {
                visible: dialog.topic === "vocabulary"
                Layout.fillWidth: true
                spacing: 12
                Label { Layout.fillWidth: true; text: "Use a short glossary for project terms that a general speech recognizer may spell unpredictably. For example:"; color: Theme.ink; wrapMode: Text.Wrap; font.pixelSize: 17 }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: glossaryExample.implicitHeight + 22
                    color: Theme.editorField
                    border.color: Theme.rule
                    radius: 7
                    Label { id: glossaryExample; anchors.fill: parent; anchors.margins: 11; text: "SBNG\nmicrocontroller\nUART\nDMA\nQt Quick\nstd::expected"; color: Theme.ink; font.family: Theme.monoFont; font.pixelSize: 16; lineHeight: 1.15 }
                }
                Label { Layout.fillWidth: true; text: "SBNG is shown as a project term with its spelling preserved. The other entries are examples of hardware, software, and C++ terms that may matter when you describe connections or modules. Keep the list short and relevant."; color: Theme.ink; wrapMode: Text.Wrap; font.pixelSize: 17 }
                Label { Layout.fillWidth: true; text: "Illustrative data flow"; color: Theme.ink; font.pixelSize: 18; font.weight: Font.DemiBold }
                Flickable {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 160
                    clip: true
                    contentWidth: diagramColumn.implicitWidth
                    contentHeight: diagramColumn.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
                    ColumnLayout {
                        id: diagramColumn
                        spacing: 12
                        RowLayout {
                            spacing: 10
                            Layout.fillWidth: true
                            ColumnLayout { spacing: 3; Icon { kind: "mic"; Layout.preferredWidth: 24; Layout.preferredHeight: 24; stroke: Theme.ink }; Label { text: "audio"; color: Theme.ink; font.pixelSize: 15 } }
                            Label { text: "→"; color: Theme.accent; font.pixelSize: 22 }
                            ColumnLayout { spacing: 3; Icon { kind: "ear"; Layout.preferredWidth: 24; Layout.preferredHeight: 24; stroke: Theme.ink }; Label { text: "Whisper"; color: Theme.ink; font.pixelSize: 15 } }
                            Label { text: "→"; color: Theme.accent; font.pixelSize: 22 }
                            ColumnLayout { spacing: 3; Icon { kind: "edit"; Layout.preferredWidth: 24; Layout.preferredHeight: 24; stroke: Theme.ink }; Label { text: "raw transcript"; color: Theme.ink; font.pixelSize: 15 } }
                            Label { text: "→"; color: Theme.accent; font.pixelSize: 22 }
                            ColumnLayout { spacing: 3; Icon { kind: "paper-pencil"; Layout.preferredWidth: 24; Layout.preferredHeight: 24; stroke: Theme.ink }; Label { text: "optional correction"; color: Theme.ink; font.pixelSize: 15 } }
                            Label { text: "→"; color: Theme.accent; font.pixelSize: 22 }
                            ColumnLayout { spacing: 3; Icon { kind: "review-ledger"; Layout.preferredWidth: 24; Layout.preferredHeight: 24; stroke: Theme.ink }; Label { text: "final text"; color: Theme.ink; font.pixelSize: 15 } }
                        }
                        RowLayout {
                            spacing: 10
                            Layout.leftMargin: 30
                            ColumnLayout { spacing: 3; Icon { kind: "book"; Layout.preferredWidth: 24; Layout.preferredHeight: 24; stroke: Theme.accent }; Label { text: "glossary"; color: Theme.ink; font.pixelSize: 15 } }
                            Label { text: "↗"; color: Theme.accent; font.pixelSize: 22 }
                            Label { text: "Whisper context"; color: Theme.inkMuted; font.family: Theme.monoFont; font.pixelSize: 15 }
                            Label { text: "   ↘ optional language-model context"; color: Theme.inkMuted; font.family: Theme.monoFont; font.pixelSize: 15 }
                        }
                    }
                }
                Label { Layout.fillWidth: true; text: "Audio is recognized locally by Whisper, then the raw transcript can pass through the optional selected local or cloud language model. The glossary is supplied as context to local Whisper and to transcript correction; Groq speech recognition does not receive it, and Vosk currently has no vocabulary pass."; color: Theme.ink; wrapMode: Text.Wrap; font.pixelSize: 17 }
                Label { Layout.fillWidth: true; text: "A hint can bias a choice or spelling. It is not training, a pronunciation dictionary, or a hard word list. A raw mistaken transcript such as “as the and g” becoming “SBNG” is hypothetical and never guaranteed. Expected test transcripts and card answers are never supplied as hints."; color: Theme.inkMuted; wrapMode: Text.Wrap; font.pixelSize: 16 }
            }

            ColumnLayout {
                visible: dialog.topic === "stt-model"
                Layout.fillWidth: true
                spacing: 12
                Label { Layout.fillWidth: true; text: "Local speech recognition runs entirely on this machine after the selected model is installed. The choice trades model size, streaming behavior, and desktop accuracy characteristics."; color: Theme.ink; wrapMode: Text.Wrap; font.pixelSize: 17 }
                Label { Layout.fillWidth: true; text: "Whisper large v3 turbo / desktop is about 874 MB and is the desktop profile. Vosk English 0.22 / balanced is about 131 MB and uses native CPU streaming. Vosk small English 0.15 is about 41 MB and is the compact profile intended for lower-memory devices."; color: Theme.ink; wrapMode: Text.Wrap; font.pixelSize: 17 }
                Label { Layout.fillWidth: true; text: "These are speech recognition models, not text-to-speech voices. The local model path and download status determine whether the selected profile is ready."; color: Theme.inkMuted; wrapMode: Text.Wrap; font.pixelSize: 16 }
            }

            ColumnLayout {
                visible: dialog.topic === "stt-acceleration"
                Layout.fillWidth: true
                spacing: 12
                Label { Layout.fillWidth: true; text: "Automatic uses Vulkan GPU inference for Whisper when the desktop helper was built with that support, then falls back to the CPU when GPU inference cannot run. Choosing CPU keeps Whisper on the CPU. There is no accuracy guarantee from selecting either path."; color: Theme.ink; wrapMode: Text.Wrap; font.pixelSize: 17 }
                Label { Layout.fillWidth: true; text: "Vosk uses the CPU in the current implementation. Acceleration affects where Whisper work runs; it does not change the selected speech model or add vocabulary hints."; color: Theme.inkMuted; wrapMode: Text.Wrap; font.pixelSize: 16 }
            }

            ColumnLayout {
                visible: dialog.topic === "tts-engine"
                Layout.fillWidth: true
                spacing: 12
                Label { Layout.fillWidth: true; text: "This setting chooses the software runtime that produces spoken audio on this machine. Piper voice uses the bundled local Piper runtime and a downloaded voice. System speech engine uses an installed operating-system Qt Text-to-Speech engine."; color: Theme.ink; wrapMode: Text.Wrap; font.pixelSize: 17 }
                Label { Layout.fillWidth: true; text: "Piper runs on this device. The system engine's available languages, accents, voices, and offline or cloud behavior depend on the installed operating-system engine. The profile selector applies only to Piper."; color: Theme.ink; wrapMode: Text.Wrap; font.pixelSize: 17 }
                Label { Layout.fillWidth: true; text: "Piper currently runs on the CPU. Downloaded Piper runtime and voice files remain available for later offline use."; color: Theme.inkMuted; wrapMode: Text.Wrap; font.pixelSize: 16 }
            }

            ColumnLayout {
                visible: dialog.topic === "tts-profile"
                Layout.fillWidth: true
                spacing: 12
                Label { Layout.fillWidth: true; text: "A Piper profile selects the actual local voice weights and their speech characteristics. Amy low uses the Amy low voice at 16,000 Hz. LJSpeech medium uses the LJSpeech medium voice at 22,050 Hz."; color: Theme.ink; wrapMode: Text.Wrap; font.pixelSize: 17 }
                Label { Layout.fillWidth: true; text: "The low and medium labels describe the profiles' quality settings and expected synthesis trade-offs. Compare them with Test model on your machine. Their downloads are nearly the same size at about 67 MB, so this setting is not a download-size switch."; color: Theme.ink; wrapMode: Text.Wrap; font.pixelSize: 17 }
                Label { Layout.fillWidth: true; text: "Profiles apply only when Local speech engine is set to Piper. They do not affect speech recognition models or the system speech engine."; color: Theme.inkMuted; wrapMode: Text.Wrap; font.pixelSize: 16 }
            }
        }
    }
}
