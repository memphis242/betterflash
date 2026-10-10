pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: pane

    required property var deck
    property int variant: 0
    property bool busy: false
    property string actionText: "Start review"
    property bool actionEnabled: true
    property string summaryText: String(deck && deck.summary !== undefined ? deck.summary : "")
    property string summaryCaption: "Automatic summary"
    property string summaryError: ""
    property bool summaryRetryVisible: false
    property bool summaryBusy: false
    property bool saveExternally: false
    signal collapseRequested()
    signal descriptionSaved(string deckId, string description)
    signal reviewRequested(string deckId)
    signal summaryRetryRequested()

    readonly property string deckId: String(deck && deck.id !== undefined ? deck.id : "")
    readonly property string deckName: String(deck && deck.name !== undefined ? deck.name : "Untitled deck")
    readonly property string parentPath: String(deck && deck.parentPath !== undefined ? deck.parentPath : "")
    readonly property string description: String(deck && deck.description !== undefined ? deck.description : "")
    readonly property bool descriptionDialogVisible: descriptionDialog.visible
    readonly property string descriptionTargetDeckId: String(descriptionDialog.targetDeckId)
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
    function openDescriptionEditor() {
        if (pane.busy || pane.deckId.length === 0)
            return
        descriptionDialog.open()
    }
    function descriptionSaveSucceeded() {
        if (saveExternally)
            descriptionDialog.close()
    }
    readonly property int visibleOutcomes: [0, 1, 2, 3, 4].filter(index => outcomeCount(index) > 0).length

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
            PaneToggleButton {
                Layout.preferredWidth: 48
                Layout.preferredHeight: 48
                Layout.alignment: Qt.AlignTop
                rightSide: true
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
                        text: pane.integer("total") + " cards · " + pane.integer("variantTotal") + " review items"
                        textFormat: Text.PlainText
                        color: Theme.inkMuted
                        font.family: Theme.monoFont
                        font.pixelSize: 10
                        ToolTip.visible: totalsHover.hovered
                        ToolTip.text: "Includes subdecks. Reversed cards and cloze groups have separate review items."
                        HoverHandler { id: totalsHover }
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
                                visible: pane.outcomeCount(modelData) > 0
                                width: pane.outcomeTotal() > 0 ? Math.max(0, outcomeBar.width - outcomeBar.spacing * Math.max(0, pane.visibleOutcomes - 1)) * pane.outcomeCount(modelData) / pane.outcomeTotal() : 0
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
                        text: pane.summaryCaption
                        textFormat: Text.PlainText
                        color: Theme.inkMuted
                        font.family: Theme.monoFont
                        font.pixelSize: 10
                    }
                    Label {
                        Layout.fillWidth: true
                        textFormat: Text.PlainText
                        text: pane.integer("due") > 0 ? (pane.summaryText.length > 0 ? pane.summaryText : "No summary is available yet.") : "Nothing is due in this deck."
                        color: Theme.inkMuted
                        font.family: Theme.contentFont
                        font.pixelSize: 12
                        lineHeight: 1.35
                        wrapMode: Text.Wrap
                    }
                    Label {
                        visible: pane.summaryBusy
                        Layout.fillWidth: true
                        text: "Generating summary..."
                        color: Theme.inkMuted
                        font.family: Theme.monoFont
                        font.pixelSize: 10
                    }
                    Label {
                        visible: pane.summaryError.length > 0
                        Layout.fillWidth: true
                        textFormat: Text.PlainText
                        text: pane.summaryError
                        color: Theme.error
                        font.family: Theme.monoFont
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                    }
                    AppButton {
                        visible: pane.summaryRetryVisible && pane.summaryError.length > 0
                        text: "Retry summary"
                        enabled: !pane.summaryBusy && !pane.busy
                        hint: "Request a new summary"
                        onClicked: pane.summaryRetryRequested()
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    FocusScope {
                        id: descriptionArea
                        Layout.fillWidth: true
                        Layout.minimumHeight: 90
                        activeFocusOnTab: true
                        implicitHeight: Math.max(90, descriptionContent.implicitHeight)
                        Accessible.role: Accessible.Button
                        Accessible.name: pane.description.length > 0 ? "Edit deck description" : "Add deck description"
                        Accessible.description: "Double-click or press Enter to edit the deck description"
                        Accessible.onPressAction: pane.openDescriptionEditor()
                        ToolTip.visible: descriptionAreaHover.hovered || descriptionArea.activeFocus
                        ToolTip.text: pane.description.length > 0 ? "Double-click to edit this deck description" : "Double-click to describe this deck"
                        HoverHandler { id: descriptionAreaHover }
                        Rectangle {
                            anchors.fill: parent
                            radius: 4
                            color: Theme.transparent
                            border.width: descriptionArea.activeFocus ? 1 : 0
                            border.color: Theme.accent
                        }
                        TapHandler {
                            acceptedButtons: Qt.LeftButton
                            onTapped: descriptionArea.forceActiveFocus()
                            onDoubleTapped: pane.openDescriptionEditor()
                            onLongPressed: pane.openDescriptionEditor()
                        }
                        Keys.onReturnPressed: function(event) {
                            pane.openDescriptionEditor()
                            event.accepted = true
                        }
                        Keys.onEnterPressed: function(event) {
                            pane.openDescriptionEditor()
                            event.accepted = true
                        }
                        Keys.onSpacePressed: function(event) {
                            pane.openDescriptionEditor()
                            event.accepted = true
                        }
                        Keys.onPressed: function(event) {
                            if (event.key === Qt.Key_F2) {
                                pane.openDescriptionEditor()
                                event.accepted = true
                            }
                        }
                        ColumnLayout {
                            id: descriptionContent
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            spacing: 8
                            Label {
                                Layout.fillWidth: true
                                text: "Deck description"
                                color: Theme.ink
                                font.family: Theme.uiFont
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                            }
                            Label {
                                Layout.fillWidth: true
                                textFormat: Text.PlainText
                                text: pane.description.length > 0 ? pane.description : "Double-click to describe this deck."
                                color: Theme.inkMuted
                                font.family: Theme.contentFont
                                font.pixelSize: 12
                                lineHeight: 1.35
                                wrapMode: Text.Wrap
                            }
                        }
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
            text: pane.actionText
            enabled: pane.actionEnabled && !pane.busy
            primary: true
            hint: pane.actionText
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
        closePolicy: pane.busy ? Popup.NoAutoClose : Popup.CloseOnEscape
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
            AppButton { text: "Cancel"; enabled: !pane.busy; onClicked: descriptionDialog.close() }
            AppButton {
                text: "Save description"
                primary: true
                enabled: !pane.busy
                onClicked: {
                    pane.descriptionSaved(descriptionDialog.targetDeckId, descriptionEditor.text)
                    if (!pane.saveExternally)
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
