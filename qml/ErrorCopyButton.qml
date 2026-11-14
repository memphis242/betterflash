import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BetterFlash.Native 1.0 as Native

ColumnLayout {
    id: control
    property string report: ""
    property string feedback: ""
    property bool copyFailed: false
    spacing: 4
    onReportChanged: {
        feedback = ""
        copyFailed = false
        if (feedbackTimer) feedbackTimer.stop()
    }
    AppButton {
        text: control.feedback === "Copied" ? "Copied" : "Copy error"
        hint: "Copy the error summary and recommendation, including details when expanded."
        enabled: control.report.length > 0
        onClicked: {
            control.copyFailed = !Native.Clipboard.copyText(control.report)
            control.feedback = control.copyFailed
                ? "The clipboard is unavailable. Select the error text and copy it manually."
                : "Copied"
            if (!control.copyFailed) feedbackTimer.restart()
        }
    }
    Label {
        visible: control.copyFailed
        text: control.feedback
        textFormat: Text.PlainText
        color: Theme.error
        font.pixelSize: 13
        wrapMode: Text.Wrap
        Layout.fillWidth: true
        Accessible.role: Accessible.AlertMessage
    }
    Timer {
        id: feedbackTimer
        interval: 1800
        onTriggered: control.feedback = ""
    }
}
