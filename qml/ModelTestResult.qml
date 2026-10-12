pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: control
    property var report: ({})
    property string error: ""
    property bool busy: false
    property string status: ""
    property string title: "Model validation"
    readonly property string outcome: busy ? "running" : report.outcome || (error ? "failed" : "")
    readonly property color resultColor: outcome === "passed" ? Theme.success
        : outcome === "failed" ? Theme.error : outcome === "running" ? Theme.warning : Theme.rule
    visible: busy || !!status || !!error || !!report.outcome
    implicitHeight: content.implicitHeight + 36
    color: Theme.surface
    radius: 8
    border.width: 1
    border.color: resultColor

    ColumnLayout {
        id: content
        x: 18
        y: 18
        width: Math.max(0, control.width - 36)
        spacing: 14
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: control.title
                color: Theme.ink
                font.pixelSize: 18
                font.bold: true
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
            Label {
                text: {
                    switch (control.outcome) {
                    case "passed": return "Passed";
                    case "failed": return "Failed";
                    case "running": return "Running";
                    case "canceled": return "Canceled";
                    default: return "";
                    }
                }
                color: control.resultColor
                font.pixelSize: 16
                font.bold: true
            }
        }
        Repeater {
            model: [
                {label: "Input", value: control.report.input || ""},
                {label: "Expected output", value: control.report.expected || ""},
                {label: "Model output", value: control.report.output || (control.busy ? "Waiting for the model..." : "No model output returned.")}
            ]
            delegate: ColumnLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 5
                visible: !!modelData.value
                Label {
                    text: parent.modelData.label
                    color: Theme.inkMuted
                    font.pixelSize: 15
                    font.bold: true
                }
                TextArea {
                    text: parent.modelData.value
                    textFormat: TextEdit.PlainText
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    color: Theme.ink
                    font.pixelSize: 16
                    padding: 0
                    Layout.fillWidth: true
                    background: null
                    Accessible.name: parent.modelData.label
                }
            }
        }
        Repeater {
            model: control.report.comparisons || []
            delegate: Rectangle {
                id: comparison
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: comparisonContent.implicitHeight + 24
                radius: 5
                color: Theme.canvas
                border.color: Theme.rule
                ColumnLayout {
                    id: comparisonContent
                    x: 12
                    y: 12
                    width: Math.max(0, comparison.width - 24)
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: comparison.modelData.label
                            color: Theme.ink
                            font.pixelSize: 16
                            font.bold: true
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                        }
                        Label {
                            text: Number(comparison.modelData.similarity).toFixed(4)
                            color: Theme.ink
                            font.pixelSize: 17
                            font.bold: true
                            ToolTip.visible: scoreHover.hovered
                            ToolTip.text: "Cosine similarity between these texts. Higher values indicate closer meaning."
                            HoverHandler { id: scoreHover }
                        }
                    }
                    Label {
                        text: comparison.modelData.query + "\n" + comparison.modelData.text
                        textFormat: Text.PlainText
                        color: Theme.inkMuted
                        font.pixelSize: 16
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
        Label {
            visible: !!control.report.details
            text: control.report.details || ""
            textFormat: Text.PlainText
            color: Theme.inkMuted
            font.pixelSize: 15
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        Label {
            visible: !!control.status && !control.error
            text: control.status
            textFormat: Text.PlainText
            color: control.resultColor === Theme.rule ? Theme.inkMuted : control.resultColor
            font.pixelSize: 16
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        ErrorBlock {
            message: control.error
            textSize: 16
            Layout.fillWidth: true
        }
    }
}
