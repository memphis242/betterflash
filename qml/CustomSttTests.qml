import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ColumnLayout {
    id: root
    required property var voice
    readonly property var tests: voice.customSttTests
    property var expectedDrafts: ({})
    Layout.fillWidth: true
    spacing: 10
    onVisibleChanged: if (!visible && tests.recording) tests.stopRecording()

    function expectedFor(entry) {
        const key = String(entry.id)
        return Object.prototype.hasOwnProperty.call(expectedDrafts, key)
            ? expectedDrafts[key] : String(entry.expected || "")
    }
    function rememberExpected(id, value) {
        const next = Object.assign({}, expectedDrafts)
        next[String(id)] = String(value)
        expectedDrafts = next
    }
    function commitDrafts() {
        for (const entry of tests.entries)
            if (Object.prototype.hasOwnProperty.call(expectedDrafts, String(entry.id)))
                tests.setExpected(entry.id, expectedDrafts[String(entry.id)])
    }

    FileDialog {
        id: recordingPicker
        title: "Choose a speech recording"
        fileMode: FileDialog.OpenFile
        nameFilters: ["WAV recordings (*.wav)", "All files (*)"]
        onAccepted: tests.addRecording(selectedFile)
    }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: customColumn.implicitHeight + 28
        color: Theme.surfaceRaised
        border.color: Theme.rule
        border.width: 1
        radius: 8

        ColumnLayout {
            id: customColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 14
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "Custom tests"
                    color: Theme.ink
                    font.family: Theme.uiFont
                    font.pixelSize: 18
                    font.bold: true
                }
                Label {
                    text: "Up to 5 recordings"
                    color: Theme.inkMuted
                    font.pixelSize: 14
                    Layout.fillWidth: true
                }
                AppButton {
                    glyph: "plus"
                    glyphSize: 18
                    text: "Add recording"
                    enabled: tests.entries.length < 5 && !tests.busy && !tests.recording
                    hint: enabled ? "Add an audio recording and expected transcript" : "You can add up to five recordings"
                    onClicked: recordingPicker.open()
                }
            }
            Label {
                Layout.fillWidth: true
                text: "Use recordings that reflect your own microphone, accent, and review commands. Each test runs only when you press Test custom set."
                color: Theme.inkMuted
                wrapMode: Text.Wrap
                font.pixelSize: 14
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                Repeater {
                    model: tests.entries
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        implicitHeight: detail.implicitHeight + 20
                        color: Theme.editorField
                        border.color: Theme.rule
                        border.width: 1
                        radius: 6

                        ColumnLayout {
                            id: detail
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 10
                            spacing: 7
                            RowLayout {
                                Layout.fillWidth: true
                                Label {
                                    text: modelData.label || ("Recording " + (index + 1))
                                    color: Theme.ink
                                    font.family: Theme.uiFont
                                    font.pixelSize: 15
                                    font.bold: true
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: modelData.status || "Ready"
                                    color: modelData.status === "Passed" ? Theme.success : modelData.status === "Failed" ? Theme.error : Theme.inkMuted
                                    font.pixelSize: 14
                                }
                                AppButton {
                                    glyph: "close"
                                    glyphSize: 16
                                    hint: "Remove this recording"
                                    onClicked: tests.removeRecording(modelData.id)
                                }
                            }
                            Label {
                                Layout.fillWidth: true
                                text: (modelData.source || "Recording is ready")
                                    + (Number(modelData.duration) > 0 ? "  " + Number(modelData.duration).toFixed(1) + " s" : "")
                                color: Theme.inkMuted
                                elide: Text.ElideMiddle
                                font.family: Theme.monoFont
                                font.pixelSize: 13
                            }
                            TextArea {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 58
                                text: root.expectedFor(modelData)
                                placeholderText: "Expected transcript"
                                font.pixelSize: 15
                                wrapMode: TextArea.Wrap
                                selectByMouse: true
                                enabled: !tests.busy && !tests.recording
                                onTextChanged: if (activeFocus) root.rememberExpected(modelData.id, text)
                                onActiveFocusChanged: if (!activeFocus) tests.setExpected(modelData.id, text)
                                Accessible.name: "Expected transcript for recording " + (index + 1)
                            }
                            Label {
                                visible: Boolean(modelData.transcript)
                                Layout.fillWidth: true
                                text: "Output: " + (modelData.transcript || "")
                                color: Theme.inkMuted
                                wrapMode: Text.Wrap
                                font.pixelSize: 14
                            }
                            Label {
                                visible: Number.isFinite(Number(modelData.wordErrorRate))
                                    || Number.isFinite(Number(modelData.accuracy))
                                Layout.fillWidth: true
                                text: {
                                    const accuracy = Number(modelData.accuracy)
                                    const wer = Number(modelData.wordErrorRate)
                                    const parts = []
                                    if (Number.isFinite(accuracy))
                                        parts.push("Accuracy " + Math.round(accuracy * 100) + "%")
                                    if (Number.isFinite(wer))
                                        parts.push("Word error rate " + Math.round(wer * 100) + "%")
                                    const edits = [modelData.substitutions, modelData.deletions, modelData.insertions]
                                    if (edits.some(value => Number.isFinite(Number(value))))
                                        parts.push("S/D/I " + [modelData.substitutions || 0, modelData.deletions || 0, modelData.insertions || 0].join("/") )
                                    return parts.join("  ·  ")
                                }
                                color: Theme.inkMuted
                                font.family: Theme.monoFont
                                font.pixelSize: 13
                                wrapMode: Text.Wrap
                                ToolTip.visible: metricsHover.hovered
                                ToolTip.text: "Accuracy compares expected and transcribed words. Word error rate is (substitutions + deletions + insertions) divided by expected words."
                                HoverHandler { id: metricsHover }
                            }
                            Label {
                                visible: Boolean(modelData.error)
                                Layout.fillWidth: true
                                text: modelData.error || ""
                                color: Theme.error
                                wrapMode: Text.Wrap
                                font.pixelSize: 14
                            }
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                AppButton {
                    text: tests.recording ? "Stop recording" : "Record new sample"
                    glyph: tests.recording ? "stop" : "mic"
                    enabled: !tests.busy && (tests.recording || tests.entries.length < 5)
                    hint: tests.recording ? "Stop recording and add the sample" : "Record a sample using the system microphone"
                    onClicked: tests.recording ? tests.stopRecording() : tests.startRecording()
                }
                Item { Layout.fillWidth: true }
                ModelActionButton {
                    text: tests.busy ? "Testing..." : "Test custom set"
                    actionKind: "test"
                    enabled: tests.canRun && !tests.recording
                    hint: enabled ? "Transcribe every custom recording" : "Add recordings with expected transcripts first"
                    onClicked: {
                        root.commitDrafts()
                        tests.runTests()
                    }
                }
                AppButton {
                    text: "Cancel"
                    visible: tests.busy
                    enabled: tests.busy
                    onClicked: tests.cancelTests()
                }
            }
            Label {
                visible: Boolean(tests.status) || tests.busy
                Layout.fillWidth: true
                text: tests.busy ? (tests.status || "Testing custom recordings...") : (tests.status || "")
                color: Theme.inkMuted
                wrapMode: Text.Wrap
                font.pixelSize: 14
            }
            Label {
                visible: tests.busy && voice.sttProvider === "groq"
                Layout.fillWidth: true
                text: "Groq will upload each selected recording for transcription."
                color: Theme.warning
                wrapMode: Text.Wrap
                font.pixelSize: 13
            }
            Label {
                visible: Boolean(tests.error)
                Layout.fillWidth: true
                text: tests.error || ""
                color: Theme.error
                wrapMode: Text.Wrap
                font.pixelSize: 14
            }
        }
    }
}
