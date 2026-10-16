import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Control {
    id: control
    required property var voice
    property var narration: null
    property bool compact: false
    readonly property var tests: voice.customSttTests
    readonly property bool narrationRecording: !!control.narration && control.narration.recording
    readonly property bool recording: (!!tests && tests.recording) || narrationRecording
    readonly property bool paused: narrationRecording ? control.narration.recordingPaused : recording && tests.recordingPaused
    readonly property bool capturing: recording ? !paused : voice.listening
    readonly property bool armed: !!voice.microphoneArmed
    readonly property color stateColor: capturing ? Theme.success : paused ? Theme.warning : Theme.inkMuted
    readonly property string stateText: recording ? (paused ? "Recording paused" : "Recording")
        : voice.listening ? "Listening" : armed ? "Mic armed" : "Mic inactive"
    readonly property string durationText: {
        const seconds = recording ? Math.floor(narrationRecording ? control.narration.recordingDuration : tests.recordingDuration) : 0
        return String(Math.floor(seconds / 60)).padStart(2, "0") + ":" + String(seconds % 60).padStart(2, "0")
    }
    readonly property string description: recording
        ? (paused ? "Microphone recording is paused; paused audio is not saved" : narrationRecording ? "Microphone is recording card narration" : "Microphone is listening and recording this custom test")
        : voice.listening ? "Microphone is listening for Voice Control commands and answers"
        : armed ? (voice.reviewMode === "always-on"
            ? "Microphone is preparing to listen continuously"
            : "Microphone is preparing to listen for one voice utterance")
        : "Microphone is not listening. " + voice.status
    visible: narrationRecording || recording || voice.enabled || armed
    implicitWidth: content.implicitWidth + leftPadding + rightPadding
    implicitHeight: 44
    leftPadding: compact ? 6 : 10
    rightPadding: leftPadding
    topPadding: 3
    bottomPadding: 3
    hoverEnabled: true
    Accessible.name: stateText
    Accessible.description: description
    ToolTip.visible: hovered
    ToolTip.text: description

    background: Rectangle {
        color: Theme.surface
        radius: 8
        border.color: control.capturing || control.paused ? control.stateColor : Theme.rule
        border.width: 1
    }
    contentItem: RowLayout {
        id: content
        spacing: 6
        Icon {
            kind: "mic"
            stroke: control.stateColor
            Layout.preferredWidth: 20
            Layout.preferredHeight: 20
        }
        Label {
            visible: !control.compact
            text: control.stateText
            textFormat: Text.PlainText
            color: control.stateColor
            font.family: Theme.uiFont
            font.pixelSize: 14
        }
        Label {
            visible: control.recording && !control.compact
            text: control.durationText
            color: Theme.ink
            font.family: Theme.monoFont
            font.pixelSize: 14
        }
        GlyphButton {
            visible: control.recording
            implicitWidth: control.compact ? 30 : 38
            implicitHeight: implicitWidth
            glyph: control.paused ? "play" : "pause"
            hint: control.paused ? "Resume microphone recording" : "Pause microphone recording"
            onClicked: control.paused ? (control.narrationRecording ? control.narration.resumeRecording() : control.tests.resumeRecording()) : (control.narrationRecording ? control.narration.pauseRecording() : control.tests.pauseRecording())
        }
        GlyphButton {
            visible: control.recording
            implicitWidth: control.compact ? 30 : 38
            implicitHeight: implicitWidth
            glyph: "stop"
            hint: "Stop and save this recording"
            onClicked: control.narrationRecording ? control.narration.finishRecording() : control.tests.stopRecording()
        }
    }
}
