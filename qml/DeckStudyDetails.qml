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
    property string summaryRetryText: "Retry summary"
    property bool summaryBusy: false
    property bool saveExternally: false
    property string mediaRoot: ""
    property bool heatmapAvailable: false
    property var activityAnalytics: ({})
    property bool activityLoading: false
    property var activityError: ({})
    property var descriptionDrafts: ({})
    signal collapseRequested()
    signal descriptionSaved(string deckId, string description)
    signal reviewRequested(string deckId)
    signal summaryRetryRequested()
    signal heatmapRequested(string deckId)
    signal activityRetryRequested()

    readonly property string deckId: String(deck && deck.id !== undefined ? deck.id : "")
    readonly property string deckName: String(deck && deck.name !== undefined ? deck.name : "Untitled deck")
    readonly property string parentPath: String(deck && deck.parentPath !== undefined ? deck.parentPath : "")
    readonly property string description: String(deck && deck.description !== undefined ? deck.description : "")
    readonly property bool descriptionEditing: pane.deckId.length > 0 && Object.prototype.hasOwnProperty.call(pane.descriptionDrafts, pane.deckId)
    readonly property var safeDeck: deck || ({})
    readonly property color panelRule: Theme.ledgerRule

    onDeckIdChanged: {
        if (pane.hasDescriptionDraft(pane.deckId)) {
            descriptionTabs.currentIndex = 0
            descriptionEditor.text = pane.draftForCurrentDeck()
            const editingDeckId = pane.deckId
            Qt.callLater(() => {
                if (!pane.visible || !pane.descriptionEditing || pane.deckId !== editingDeckId)
                    return
                descriptionEditor.forceActiveFocus()
                descriptionEditor.cursorPosition = descriptionEditor.length
            })
        }
    }

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
        if (!pane.hasDescriptionDraft(pane.deckId)) {
            const drafts = Object.assign({}, pane.descriptionDrafts)
            drafts[pane.deckId] = pane.description
            pane.descriptionDrafts = drafts
        }
        descriptionTabs.currentIndex = 0
        descriptionEditor.text = pane.draftForCurrentDeck()
        const editingDeckId = pane.deckId
        Qt.callLater(() => {
            if (!pane.visible || !pane.descriptionEditing || pane.deckId !== editingDeckId)
                return
            descriptionEditor.forceActiveFocus()
            descriptionEditor.cursorPosition = descriptionEditor.length
        })
    }
    function hasDescriptionDraft(id) {
        return id.length > 0 && Object.prototype.hasOwnProperty.call(pane.descriptionDrafts, id)
    }
    function draftForCurrentDeck() {
        return pane.hasDescriptionDraft(pane.deckId) ? String(pane.descriptionDrafts[pane.deckId]) : pane.description
    }
    function updateDescriptionDraft(text) {
        if (!pane.hasDescriptionDraft(pane.deckId))
            return
        if (String(pane.descriptionDrafts[pane.deckId]) === String(text))
            return
        const drafts = Object.assign({}, pane.descriptionDrafts)
        drafts[pane.deckId] = String(text)
        pane.descriptionDrafts = drafts
    }
    function discardDescriptionDraft(deckId) {
        const id = String(deckId || "")
        if (id.length === 0 || !Object.prototype.hasOwnProperty.call(pane.descriptionDrafts, id))
            return
        const drafts = Object.assign({}, pane.descriptionDrafts)
        delete drafts[id]
        pane.descriptionDrafts = drafts
    }
    function cancelDescriptionEdit() {
        if (!pane.busy)
            pane.discardDescriptionDraft(pane.deckId)
    }
    function saveDescription() {
        if (!pane.hasDescriptionDraft(pane.deckId) || pane.busy)
            return
        const id = pane.deckId
        const text = String(pane.descriptionDrafts[id])
        pane.descriptionSaved(id, text)
        if (!pane.saveExternally)
            pane.descriptionSaveSucceeded(id, text)
    }
    function descriptionSaveSucceeded(savedDeckId, savedDescription) {
        const id = String(savedDeckId || "")
        if (id.length === 0 || !Object.prototype.hasOwnProperty.call(pane.descriptionDrafts, id))
            return
        if (String(pane.descriptionDrafts[id]) !== String(savedDescription))
            return
        pane.discardDescriptionDraft(id)
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
                    visible: pane.heatmapAvailable
                    Layout.fillWidth: true
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: "Study activity"
                            color: Theme.inkMuted
                            font.family: Theme.monoFont
                            font.pixelSize: 11
                            font.capitalization: Font.AllUppercase
                        }
                        GlyphButton {
                            glyph: "calendar-page"
                            hint: "Open the full calendar heatmap for this deck"
                            enabled: pane.deckId.length > 0
                            onClicked: pane.heatmapRequested(pane.deckId)
                        }
                    }
                    CalendarHeatmap {
                        visible: !pane.activityError.code
                        Layout.fillWidth: true
                        Layout.preferredHeight: implicitHeight
                        compact: true
                        days: pane.activityAnalytics.days || []
                        today: String(pane.activityAnalytics.today || "")
                        firstDate: String(pane.activityAnalytics.firstDate || "")
                    }
                    Label {
                        visible: pane.activityLoading
                        Layout.fillWidth: true
                        text: "Loading study activity..."
                        color: Theme.inkMuted
                        font.family: Theme.uiFont
                        font.pixelSize: 11
                    }
                    ErrorBlock {
                        visible: Boolean(pane.activityError.code)
                        Layout.fillWidth: true
                        code: String(pane.activityError.code || "")
                        message: String(pane.activityError.message || "")
                        details: String(pane.activityError.detail || "")
                    }
                    AppButton {
                        visible: Boolean(pane.activityError.code)
                        text: "Retry activity"
                        enabled: !pane.activityLoading
                        onClicked: pane.activityRetryRequested()
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
                    MarkdownPane {
                        visible: pane.integer("due") > 0 && pane.summaryText.length > 0
                        Layout.fillWidth: true
                        markdown: pane.summaryText
                        mediaRoot: pane.mediaRoot
                        baseFontSize: 14
                    }
                    Label {
                        visible: pane.integer("due") > 0 && pane.summaryText.length === 0 && !pane.summaryBusy && pane.summaryError.length === 0
                        Layout.fillWidth: true
                        text: "No summary has been generated for this deck."
                        color: Theme.inkMuted
                        font.family: Theme.contentFont
                        font.pixelSize: 13
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
                        visible: pane.summaryRetryVisible
                        text: pane.summaryRetryText
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
                        Layout.minimumHeight: pane.descriptionEditing ? 220 : 90
                        activeFocusOnTab: true
                        implicitHeight: pane.descriptionEditing
                            ? Math.max(220, descriptionContent.implicitHeight)
                            : Math.max(90, descriptionPreview.implicitHeight + 36)
                        Accessible.role: Accessible.EditableText
                        Accessible.name: pane.description.length > 0 ? "Deck description" : "Empty deck description"
                        Accessible.description: "Double-click or press Enter to edit the deck description"
                        Accessible.onPressAction: pane.openDescriptionEditor()
                        HoverHandler {
                            cursorShape: Qt.IBeamCursor
                        }
                        TapHandler {
                            enabled: !pane.descriptionEditing
                            acceptedButtons: Qt.LeftButton
                            onTapped: descriptionArea.forceActiveFocus()
                            onDoubleTapped: pane.openDescriptionEditor()
                            onLongPressed: pane.openDescriptionEditor()
                        }
                        Keys.onPressed: function(event) {
                            const enter = event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                            if (event.key === Qt.Key_F2) {
                                pane.openDescriptionEditor()
                                event.accepted = true
                            } else if (!pane.descriptionEditing
                                       && (enter || event.key === Qt.Key_Space)) {
                                pane.openDescriptionEditor()
                                event.accepted = true
                            } else if (pane.descriptionEditing && enter && (event.modifiers & Qt.ControlModifier)) {
                                pane.saveDescription()
                                event.accepted = true
                            } else if (pane.descriptionEditing && event.key === Qt.Key_Escape) {
                                pane.cancelDescriptionEdit()
                                event.accepted = true
                            }
                        }
                        Shortcut {
                            sequences: ["Ctrl+Return", "Ctrl+Enter"]
                            enabled: pane.visible && descriptionArea.activeFocus && pane.descriptionEditing && !pane.busy
                            onActivated: pane.saveDescription()
                        }
                        Shortcut {
                            sequence: "Escape"
                            enabled: pane.visible && descriptionArea.activeFocus && pane.descriptionEditing && !pane.busy
                            onActivated: pane.cancelDescriptionEdit()
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
                            Item {
                                id: descriptionPreview
                                visible: !pane.descriptionEditing
                                Layout.fillWidth: true
                                implicitHeight: descriptionPreviewContent.implicitHeight
                                ColumnLayout {
                                    id: descriptionPreviewContent
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    spacing: 8
                                    MarkdownPane {
                                        Layout.fillWidth: true
                                        markdown: pane.description.length > 0 ? pane.description : "_Double-click to describe this deck._"
                                        mediaRoot: pane.mediaRoot
                                        baseFontSize: 15
                                        opacity: pane.description.length > 0 ? 1 : 0.72
                                    }
                                }
                            }
                            ColumnLayout {
                                visible: pane.descriptionEditing
                                Layout.fillWidth: true
                                spacing: 8
                                TabBar {
                                    id: descriptionTabs
                                    Layout.fillWidth: true
                                    TabButton {
                                        id: descriptionSourceTab
                                        text: "Source"
                                        background: Rectangle {
                                            radius: 7
                                            color: descriptionSourceTab.checked ? Theme.editorTabActive : Theme.editorTabInactive
                                            border.color: descriptionSourceTab.checked ? Theme.editorTabActive : Theme.rule
                                        }
                                        contentItem: Label {
                                            text: descriptionSourceTab.text
                                            color: descriptionSourceTab.checked ? Theme.editorTabActiveInk : Theme.inkMuted
                                            font.family: Theme.monoFont
                                            font.pixelSize: 15
                                            font.bold: descriptionSourceTab.checked
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                    }
                                    TabButton {
                                        id: descriptionPreviewTab
                                        text: "Preview"
                                        background: Rectangle {
                                            radius: 7
                                            color: descriptionPreviewTab.checked ? Theme.editorTabActive : Theme.editorTabInactive
                                            border.color: descriptionPreviewTab.checked ? Theme.editorTabActive : Theme.rule
                                        }
                                        contentItem: Label {
                                            text: descriptionPreviewTab.text
                                            color: descriptionPreviewTab.checked ? Theme.editorTabActiveInk : Theme.inkMuted
                                            font.family: Theme.monoFont
                                            font.pixelSize: 15
                                            font.bold: descriptionPreviewTab.checked
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                    }
                                    onCurrentIndexChanged: {
                                        if (currentIndex !== 0)
                                            return
                                        const editingDeckId = pane.deckId
                                        Qt.callLater(() => {
                                            if (!pane.visible || !pane.descriptionEditing || pane.deckId !== editingDeckId)
                                                return
                                            descriptionEditor.forceActiveFocus()
                                            descriptionEditor.cursorPosition = descriptionEditor.length
                                        })
                                    }
                                }
                                StackLayout {
                                    id: descriptionStack
                                    currentIndex: descriptionTabs.currentIndex
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: Math.max(110, Math.min(260, descriptionEditor.implicitHeight + 28))
                                    Item {
                                        id: descriptionSourceViewport
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        Rectangle {
                                            anchors.fill: parent
                                            radius: 9
                                            color: Theme.editorField
                                            border.width: descriptionEditor.activeFocus ? 2 : 1
                                            border.color: descriptionEditor.activeFocus ? Theme.accent : Theme.rule
                                        }
                                        ScrollView {
                                            id: descriptionSourceScroll
                                            anchors.fill: parent
                                            clip: true
                                            contentWidth: availableWidth
                                            TextArea {
                                                id: descriptionEditor
                                                objectName: "deckDescriptionEditor"
                                                width: descriptionSourceScroll.availableWidth
                                                height: Math.max(descriptionSourceScroll.availableHeight, implicitHeight)
                                                text: pane.draftForCurrentDeck()
                                                textFormat: TextEdit.PlainText
                                                readOnly: pane.busy
                                                color: Theme.ink
                                                font.family: Theme.monoFont
                                                font.pixelSize: 16
                                                wrapMode: TextEdit.Wrap
                                                leftPadding: 14
                                                rightPadding: 14
                                                topPadding: 12
                                                bottomPadding: 12
                                                cursorVisible: true
                                                placeholderText: "Describe this deck in Markdown."
                                                selectByMouse: true
                                                background: Rectangle {
                                                    color: Theme.transparent
                                                }
                                                onTextChanged: pane.updateDescriptionDraft(text)
                                                Keys.onShortcutOverride: event => {
                                                    const enter = event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                                                    if ((enter && (event.modifiers & Qt.ControlModifier)) || event.key === Qt.Key_Escape)
                                                        event.accepted = true
                                                }
                                                Keys.onPressed: event => {
                                                    switch (event.key) {
                                                    case Qt.Key_Return:
                                                    case Qt.Key_Enter:
                                                        if (event.modifiers & Qt.ControlModifier) {
                                                            pane.saveDescription()
                                                            event.accepted = true
                                                        }
                                                        break
                                                    case Qt.Key_Escape:
                                                        pane.cancelDescriptionEdit()
                                                        event.accepted = true
                                                        break
                                                    }
                                                }
                                            }
                                        }
                                    }
                                    ScrollView {
                                        clip: true
                                        contentWidth: availableWidth
                                        MarkdownPane {
                                            width: parent.width
                                            markdown: pane.draftForCurrentDeck()
                                            mediaRoot: pane.mediaRoot
                                            baseFontSize: 15
                                        }
                                    }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    AppButton {
                                        Layout.fillWidth: true
                                        text: "Save description"
                                        enabled: !pane.busy
                                        primary: true
                                        hint: "Save description changes (Ctrl+Enter)"
                                        onClicked: pane.saveDescription()
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Item { Layout.fillWidth: true }
                                        AppButton {
                                            text: "Cancel"
                                            enabled: !pane.busy
                                            hint: "Discard description changes"
                                            onClicked: pane.cancelDescriptionEdit()
                                        }
                                    }
                                }
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

}
