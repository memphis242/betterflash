pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: panel
    property var reports: ({})
    property var draft: ({})
    signal dismissRequested(string mode)
    color: Theme.surface
    border.color: Theme.warning
    radius: 8
    implicitHeight: 230
    implicitWidth: 330
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8
        Label {
            text: "Check findings"
            color: Theme.warning
            font.bold: true
            font.pixelSize: 17
        }
        Label {
            text: "LLMs can be wrong. Review each claim before changing the card."
            color: Theme.inkMuted
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            font.pixelSize: 12
        }
        ScrollView {
            id: viewport
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: viewport.availableWidth
                spacing: 14
                Repeater {
                    model: ["correctness", "completeness"].filter(mode => !!panel.reports[mode])
                    delegate: ColumnLayout {
                        id: check
                        required property string modelData
                        readonly property var entry: panel.reports[modelData] || ({})
                        readonly property var report: entry.report || ({})
                        readonly property bool edited: entry.front !== panel.draft.front || entry.back !== panel.draft.back || entry.kind !== panel.draft.kind
                        Layout.fillWidth: true
                        spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                text: check.modelData === "correctness" ? "Correctness" : "Completeness"
                                font.bold: true
                                color: check.report.verdict === "no_issues" ? Theme.success : Theme.warning
                                Layout.fillWidth: true
                            }
                            GlyphButton {
                                glyph: "close"
                                hint: "Dismiss these " + check.modelData + " findings"
                                onClicked: panel.dismissRequested(check.modelData)
                            }
                        }
                        Label {
                            visible: check.edited
                            text: "Edited since this check. Findings refer to the checked text; run a new check to verify your changes."
                            textFormat: Text.PlainText
                            color: Theme.warning
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                        TextArea {
                            id: summaryText
                            ContextMenu.menu: TextEditingMenu { editor: summaryText }
                            text: check.report.summary || ""
                            textFormat: TextEdit.PlainText
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.Wrap
                            color: Theme.ink
                            padding: 0
                            background: null
                            Layout.fillWidth: true
                        }
                        Repeater {
                            model: check.report.findings || []
                            delegate: ColumnLayout {
                                id: finding
                                required property var modelData
                                required property int index
                                Layout.fillWidth: true
                                spacing: 6
                                TextArea {
                                    id: claimText
                                    ContextMenu.menu: TextEditingMenu { editor: claimText }
                                    text: (finding.index + 1) + ". " + finding.modelData.claim
                                    textFormat: TextEdit.PlainText
                                    color: Theme.warning
                                    font.bold: true
                                    readOnly: true
                                    selectByMouse: true
                                    wrapMode: TextEdit.Wrap
                                    padding: 0
                                    background: null
                                    Layout.fillWidth: true
                                }
                                TextArea {
                                    id: explanationText
                                    ContextMenu.menu: TextEditingMenu { editor: explanationText }
                                    text: finding.modelData.explanation + (finding.modelData.suggestion ? "\n\nSuggested adjustment: " + finding.modelData.suggestion : "")
                                        + ((finding.modelData.sourceIds || []).length ? "\n\nSources: " + finding.modelData.sourceIds.join(", ") : "")
                                    textFormat: TextEdit.PlainText
                                    color: Theme.ink
                                    font.pixelSize: 13
                                    readOnly: true
                                    selectByMouse: true
                                    wrapMode: TextEdit.Wrap
                                    padding: 0
                                    background: null
                                    Layout.fillWidth: true
                                }
                            }
                        }
                        Repeater {
                            model: check.report.sources || []
                            delegate: Label {
                                required property var modelData
                                text: (modelData.id || "") + ": " + (modelData.title || modelData.url || "")
                                textFormat: Text.PlainText
                                color: Theme.inkMuted
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                                ToolTip.visible: sourceHover.hovered
                                ToolTip.text: modelData.url || ""
                                HoverHandler { id: sourceHover }
                            }
                        }
                    }
                }
            }
        }
    }
}
