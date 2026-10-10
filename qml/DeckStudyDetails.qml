pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: pane

    required property var deck
    property int variant: 0
    signal collapseRequested()
    signal descriptionSaved(string deckId, string description)
    signal reviewRequested(string deckId)

    readonly property string deckId: String(deck && deck.id !== undefined ? deck.id : "")
    readonly property string deckName: String(deck && deck.name !== undefined ? deck.name : "Untitled deck")
    readonly property string parentPath: String(deck && deck.parentPath !== undefined ? deck.parentPath : "")
    readonly property string description: String(deck && deck.description !== undefined ? deck.description : "")
    readonly property var safeDeck: deck || ({})
    readonly property color panelRule: Theme.ledgerRule

    color: Theme.ledgerSurface
    border.color: panelRule
    border.width: 0
    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: pane.panelRule
    }
    implicitWidth: 360
    implicitHeight: 650

    function number(key, fallback) {
        const value = Number(safeDeck[key])
        return Number.isFinite(value) ? Math.max(0, value) : fallback
    }
    function integer(key) {
        return Math.round(number(key, 0))
    }
    function percent(value) {
        return Math.round(Math.max(0, Math.min(1, Number(value) || 0)) * 100) + "%"
    }
    function outcomeCount(index) {
        const values = safeDeck.outcomes
        const value = values && values[index] !== undefined ? Number(values[index]) : 0
        return Number.isFinite(value) ? Math.max(0, value) : 0
    }
    function outcomeTotal() {
        let total = 0
        for (let index = 0; index < 5; ++index)
            total += outcomeCount(index)
        return total
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.bottomMargin: 14
            spacing: 10

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                Label {
                    Layout.fillWidth: true
                    visible: pane.parentPath.length > 0
                    textFormat: Text.PlainText
                    text: pane.parentPath
                    color: Theme.inkMuted
                    font.family: Theme.monoFont
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    text: pane.deckName
                    textFormat: Text.PlainText
                    color: Theme.ink
                    font.family: Theme.uiFont
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                    wrapMode: Text.Wrap
                }
            }
            GlyphButton {
                Layout.preferredWidth: 38
                Layout.preferredHeight: 38
                Layout.alignment: Qt.AlignTop
                glyph: "arrow-right"
                glyphSize: 19
                hint: "Collapse deck details"
                onClicked: pane.collapseRequested()
                Accessible.onPressAction: pane.collapseRequested()
            }
        }

        ScrollView {
            id: detailsScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ScrollBar.vertical.policy: ScrollBar.AsNeeded

            ColumnLayout {
                width: detailsScroll.availableWidth
                spacing: 22

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Label {
                        text: "Review queue"
                        color: Theme.inkMuted
                        font.family: Theme.monoFont
                        font.pixelSize: 11
                        font.capitalization: Font.AllUppercase
                    }
                    Label {
                        Layout.fillWidth: true
                        text: pane.integer("total") + " total cards including subdecks"
                        textFormat: Text.PlainText
                        color: Theme.inkMuted
                        font.family: Theme.monoFont
                        font.pixelSize: 10
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Repeater {
                            model: [
                                {label: "Due now", value: pane.integer("due")},
                                {label: "New due", value: pane.integer("newDue")},
                                {label: "Review due", value: pane.integer("reviewDue")},
                                {label: "Due later", value: pane.integer("later")}
                            ]
                            delegate: ColumnLayout {
                                id: queueStat
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 0
                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 34
                                    spacing: 8
                                    Label {
                                        Layout.fillWidth: true
                                        text: queueStat.modelData.label
                                        color: Theme.inkMuted
                                        font.family: Theme.uiFont
                                        font.pixelSize: queueStat.modelData.label === "Due now" ? 13 : 12
                                        font.weight: queueStat.modelData.label === "Due now" ? Font.DemiBold : Font.Normal
                                    }
                                    Label {
                                        text: String(queueStat.modelData.value)
                                        color: queueStat.modelData.label === "Due now" ? Theme.accent : Theme.ink
                                        font.family: Theme.monoFont
                                        font.pixelSize: queueStat.modelData.label === "Due now" ? 16 : 15
                                        font.weight: Font.DemiBold
                                    }
                                }
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 1
                                    color: pane.panelRule
                                    opacity: queueStat.modelData.label === "Due later" ? 0 : 0.65
                                }
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 9
                    Label {
                        text: "Performance"
                        color: Theme.inkMuted
                        font.family: Theme.monoFont
                        font.pixelSize: 11
                        font.capitalization: Font.AllUppercase
                    }
                    RowLayout {
                        visible: pane.integer("reviewCount") > 0
                        Layout.fillWidth: true
                        spacing: 18
                        Label {
                            text: pane.percent(pane.number("recall", 0))
                            color: Theme.ink
                            font.family: Theme.monoFont
                            font.pixelSize: 25
                            font.weight: Font.DemiBold
                            ToolTip.visible: perfHover.hovered
                            ToolTip.text: "Average recall across the recorded review sample"
                            HoverHandler { id: perfHover }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            Label {
                                text: "average recall"
                                color: Theme.inkMuted
                                font.family: Theme.uiFont
                                font.pixelSize: 12
                            }
                            Label {
                                text: pane.integer("reviewCount") + " reviews"
                                color: Theme.inkMuted
                                font.family: Theme.monoFont
                                font.pixelSize: 10
                            }
                            Label {
                                text: pane.number("seconds", 0).toFixed(1) + " s average response"
                                color: Theme.inkMuted
                                font.family: Theme.monoFont
                                font.pixelSize: 10
                            }
                        }
                    }
                    Row {
                        id: outcomeBar
                        visible: pane.integer("reviewCount") > 0
                        Layout.fillWidth: true
                        Layout.preferredHeight: 12
                        spacing: 2
                        Repeater {
                            model: [0, 1, 2, 3, 4]
                            delegate: Rectangle {
                                required property int modelData
                                width: pane.outcomeTotal() > 0 ? (outcomeBar.width - outcomeBar.spacing * 4) * pane.outcomeCount(modelData) / pane.outcomeTotal() : 0
                                height: 10
                                radius: 2
                                color: [Theme.recallMissed, Theme.recallPartial, Theme.recallHard, Theme.recallGood, Theme.recallEasy][modelData]
                                ToolTip.visible: outcomeHover.hovered
                                ToolTip.text: ["Missed", "Partial", "Hard", "Good", "Easy"][modelData] + ": " + pane.outcomeCount(modelData)
                                HoverHandler { id: outcomeHover }
                            }
                        }
                    }
                    Label {
                        visible: pane.integer("reviewCount") <= 0
                        text: "No reviews yet."
                        textFormat: Text.PlainText
                        color: Theme.inkMuted
                        font.family: Theme.uiFont
                        font.pixelSize: 12
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Label {
                        text: "Remaining to review"
                        color: Theme.ink
                        font.family: Theme.uiFont
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }
                    Label {
                        visible: pane.integer("due") > 0
                        Layout.fillWidth: true
                        text: "Automatic summary"
                        textFormat: Text.PlainText
                        color: Theme.inkMuted
                        font.family: Theme.monoFont
                        font.pixelSize: 10
                    }
                    Label {
                        Layout.fillWidth: true
                        textFormat: Text.PlainText
                        text: pane.integer("due") > 0 ? String(pane.safeDeck.summary || "No summary is available yet.") : "Nothing is due in this deck."
                        color: Theme.inkMuted
                        font.family: Theme.contentFont
                        font.pixelSize: 12
                        lineHeight: 1.35
                        wrapMode: Text.Wrap
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: "Deck description"
                            color: Theme.ink
                            font.family: Theme.uiFont
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }
                        GlyphButton {
                            glyph: pane.description.length > 0 ? "edit" : "plus"
                            glyphSize: 16
                            hint: pane.description.length > 0 ? "Edit deck description" : "Add deck description"
                            onClicked: descriptionDialog.open()
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        textFormat: Text.PlainText
                        text: pane.description.length > 0 ? pane.description : "Add a short note about what this deck covers."
                        color: Theme.inkMuted
                        font.family: Theme.contentFont
                        font.pixelSize: 12
                        lineHeight: 1.35
                        wrapMode: Text.Wrap
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: 14
            Layout.bottomMargin: 14
            Layout.preferredHeight: 1
            color: pane.panelRule
        }
        AppButton {
            Layout.fillWidth: true
            text: "Start review"
            primary: true
            hint: "Review this deck"
            onClicked: pane.reviewRequested(pane.deckId)
        }
    }

    Dialog {
        id: descriptionDialog
        property string targetDeckId: ""
        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: Math.min(560, Math.max(320, (Overlay.overlay ? Overlay.overlay.width : 560) - 40))
        padding: 20
        title: pane.description.length > 0 ? "Edit deck description" : "Add deck description"
        closePolicy: Popup.CloseOnEscape
        background: Rectangle {
            color: Theme.surface
            radius: 7
            border.color: Theme.rule
        }
        Overlay.modal: Rectangle { color: Theme.scrim }
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: "A short note helps explain this deck's focus."
                color: Theme.inkMuted
                font.family: Theme.uiFont
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 150
                contentWidth: availableWidth
                clip: true
                TextArea {
                    id: descriptionEditor
                    width: parent.width
                    wrapMode: TextEdit.Wrap
                    selectByMouse: true
                    color: Theme.ink
                    placeholderText: "Describe this deck"
                    background: Rectangle {
                        color: Theme.ledgerSurface
                        border.color: descriptionEditor.activeFocus ? Theme.accent : Theme.rule
                        radius: 4
                    }
                }
            }
        }
        footer: RowLayout {
            spacing: 10
            Item { Layout.fillWidth: true }
            AppButton { text: "Cancel"; onClicked: descriptionDialog.close() }
            AppButton {
                text: "Save description"
                primary: true
                onClicked: {
                    pane.descriptionSaved(descriptionDialog.targetDeckId, descriptionEditor.text)
                    descriptionDialog.close()
                }
            }
        }
        onOpened: {
            targetDeckId = pane.deckId
            descriptionEditor.text = pane.description
            descriptionEditor.forceActiveFocus()
        }
    }
}
