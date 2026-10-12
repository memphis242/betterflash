import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: details
    required property var ui
    property var session: ({})
    signal collapseRequested
    readonly property var outcomes: session.outcomes || [0, 0, 0, 0, 0]
    readonly property var colors: [Theme.recallMissed, Theme.recallPartial, Theme.recallHard, Theme.recallGood, Theme.recallEasy]
    readonly property var labels: ["Missed", "Partial", "Hard", "Good", "Easy"]
    Rectangle {
        anchors.fill: parent
        color: Theme.ledgerSurface
        border.width: 1
        border.color: Theme.ledgerRule
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            Label {
                textFormat: Text.PlainText
                text: "Review details"
                color: Theme.ink
                font.family: Theme.contentFont
                font.pixelSize: details.width < 340 ? 18 : 22
                font.weight: Font.DemiBold
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                elide: Text.ElideRight
            }
            PaneToggleButton {
                rightSide: true
                hint: "Collapse review details"
                onClicked: details.collapseRequested()
            }
        }
        ScrollView {
            id: statsScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ScrollBar.vertical: ScrollBar {}
            ColumnLayout {
                id: stats
                width: statsScroll.availableWidth
                spacing: 16
                Label {
                    textFormat: Text.PlainText
                    text: String(details.session.deckName || "Review")
                    color: Theme.ink
                    font.family: Theme.contentFont
                    font.pixelSize: 19
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    wrapMode: Text.Wrap
                }
                Label {
                    textFormat: Text.PlainText
                    text: details.ui.dateTime(details.session.startedAt) + " - " + details.ui.dateTime(details.session.lastReviewedAt)
                    color: Theme.inkMuted
                    font.family: Theme.monoFont
                    font.pixelSize: 11
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    wrapMode: Text.Wrap
                }
                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: Theme.ledgerRule
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: stats.width < 280 ? 1 : 2
                    columnSpacing: 22
                    rowSpacing: 8
                    Label {
                        textFormat: Text.PlainText
                        text: Math.round(Number(details.session.averageRecall || 0) * 100) + "%"
                        color: details.recallColor
                        font.family: Theme.monoFont
                        font.pixelSize: 32
                        font.weight: Font.DemiBold
                        Layout.minimumWidth: 0
                        HoverHandler {
                            id: recallHover
                        }
                        ToolTip.visible: recallHover.hovered
                        ToolTip.text: "Average recalled fraction across all answers in this review session."
                    }
                    ColumnLayout {
                        spacing: 3
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Label {
                            text: "Overall recall"
                            color: Theme.ink
                            font.family: Theme.contentFont
                            font.pixelSize: 14
                            Layout.minimumWidth: 0
                        }
                        Label {
                            text: Number(details.session.cardCount || 0) + " distinct cards"
                            color: Theme.inkMuted
                            font.family: Theme.contentFont
                            font.pixelSize: 12
                            Layout.minimumWidth: 0
                            elide: Text.ElideRight
                            ToolTip.visible: distinctHover.hovered
                            ToolTip.text: "Distinct source cards reviewed in this session."
                            HoverHandler {
                                id: distinctHover
                            }
                        }
                        Label {
                            text: Number(details.session.reviewCount || 0) + " answers recorded"
                            color: Theme.inkMuted
                            font.family: Theme.monoFont
                            font.pixelSize: 11
                            Layout.minimumWidth: 0
                            elide: Text.ElideRight
                            ToolTip.visible: variantHover.hovered
                            ToolTip.text: "Answer count includes separate card variants."
                            HoverHandler {
                                id: variantHover
                            }
                        }
                    }
                }
                Label {
                    textFormat: Text.PlainText
                    text: "Outcome"
                    color: Theme.inkMuted
                    font.family: Theme.monoFont
                    font.pixelSize: 12
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Repeater {
                        model: 5
                        delegate: RowLayout {
                            required property int index
                            Layout.fillWidth: true
                            Label {
                                text: details.labels[index]
                                color: details.colors[index]
                                font.family: Theme.contentFont
                                font.pixelSize: 13
                                Layout.preferredWidth: 64
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                height: 8
                                radius: 4
                                color: Theme.surface
                                border.width: 1
                                border.color: Theme.rule
                                Rectangle {
                                    width: parent.width * Math.min(1, Number(details.outcomes[index] || 0) / Math.max(1, Number(details.session.reviewCount || 0)))
                                    height: parent.height
                                    radius: 4
                                    color: details.colors[index]
                                }
                            }
                            Label {
                                text: Number(details.outcomes[index] || 0)
                                color: Theme.ink
                                font.family: Theme.monoFont
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignRight
                                Layout.preferredWidth: 28
                            }
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: Theme.ledgerRule
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: stats.width < 320 ? 1 : 2
                    rowSpacing: 11
                    columnSpacing: 18
                    Label {
                        text: "Total answering time"
                        color: Theme.inkMuted
                        font.family: Theme.contentFont
                        font.pixelSize: 13
                        Layout.minimumWidth: 0
                        wrapMode: Text.Wrap
                    }
                    Label {
                        text: details.formatSeconds(details.session.totalResponseSeconds)
                        color: Theme.ink
                        font.family: Theme.monoFont
                        font.pixelSize: 13
                        Layout.fillWidth: true
                    }
                    Label {
                        text: "Average response"
                        color: Theme.inkMuted
                        font.family: Theme.contentFont
                        font.pixelSize: 13
                        Layout.minimumWidth: 0
                        wrapMode: Text.Wrap
                    }
                    Label {
                        text: details.formatSeconds(details.session.averageResponseSeconds)
                        color: Theme.ink
                        font.family: Theme.monoFont
                        font.pixelSize: 13
                        Layout.fillWidth: true
                    }
                    Label {
                        text: "Best successful time"
                        color: Theme.inkMuted
                        font.family: Theme.contentFont
                        font.pixelSize: 13
                        Layout.minimumWidth: 0
                        wrapMode: Text.Wrap
                        ToolTip.visible: bestTimeHover.hovered
                        ToolTip.text: "Fastest answer with a Hard, Good, or Easy result."
                        HoverHandler {
                            id: bestTimeHover
                        }
                    }
                    Label {
                        text: details.session.bestSuccessfulSeconds === null || details.session.bestSuccessfulSeconds === undefined ? "-" : details.formatSeconds(details.session.bestSuccessfulSeconds)
                        color: Theme.ink
                        font.family: Theme.monoFont
                        font.pixelSize: 13
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                    }
                }
                Label {
                    visible: Boolean(details.session.estimated)
                    textFormat: Text.PlainText
                    text: "Estimated session: consecutive reviews of the same deck on one UTC date, with no gap over 30 minutes."
                    color: Theme.inkMuted
                    font.family: Theme.contentFont
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                }
                Item {
                    Layout.fillHeight: true
                }
            }
        }
    }
    readonly property color recallColor: {
        const grade = Math.max(0, Math.min(4, Math.round(Number(session.averageGrade || 0))));
        return colors[grade];
    }
    function formatSeconds(value) {
        const seconds = Number(value || 0);
        const rounded = Math.round(seconds);
        return rounded >= 60 ? Math.floor(rounded / 60) + "m " + (rounded % 60) + "s" : seconds.toFixed(1) + " s";
    }
}
