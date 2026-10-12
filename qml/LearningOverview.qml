pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore

Item {
    id: learning
    required property var ui
    property bool active: false
    readonly property bool narrow: width < 950
    readonly property var stats: activity.analytics.stats || ({})
    readonly property bool hasStats: Object.keys(stats).length > 0
    readonly property bool hasError: Boolean(activity.error.code)
    readonly property string scopeName: selectedDeckIds.length === 0
        ? "Entire library"
        : selectedDeckIds.length === 1
            ? String((app.decks.find(deck => String(deck.id) === String(selectedDeckIds[0])) || {}).name || "Selected deck")
            : selectedDeckIds.length + " decks"
    property Settings preferences: Settings {
        category: "historyLearning"
        property string deckId: ""
        property var deckIds: []
        property bool deckSelectionMigrated: false
    }
    readonly property var selectedDeckIds: sequenceValues(preferences.deckIds)
    property bool decksSnapshotReady: false
    StudyActivityModel {
        id: activity
        deckIds: learning.selectedDeckIds
        active: learning.active
    }
    readonly property color recallColor: stats.averageRecall === null || stats.averageRecall === undefined ? Theme.inkMuted
        : [Theme.recallMissed, Theme.recallPartial, Theme.recallHard, Theme.recallGood, Theme.recallEasy]
            [Math.max(0, Math.min(4, Math.round(Number(stats.averageGrade || 0))))]
    readonly property var measures: [
        {label: "Practice score", value: hasStats && Number(stats.eligibleDecks || 0) > 0 ? Math.round(Number(stats.practiceScore || 0)) + " / 100" : "No cards", color: Theme.ink,
            hint: "Provisional score for the last 30 days. For each nonempty deck, add the latest recall of its reviewed items and divide by every current review item, including unreviewed items at zero. Average those deck scores equally. Empty folders are excluded."},
        {label: "Recall", value: stats.averageRecall === null || stats.averageRecall === undefined ? "Not yet" : Math.round(Number(stats.averageRecall) * 100) + "%", color: recallColor,
            hint: "Average latest recall among review items answered in the last 30 days. Unlike the practice score, this excludes unreviewed items. " + Number(stats.reviewedVariants || 0) + " of " + Number(stats.totalVariants || 0) + " current review items reached."},
        {label: "Decks reached", value: Number(stats.reviewedDecks || 0) + " / " + Number(stats.eligibleDecks || 0), color: Theme.ink,
            hint: "Nonempty decks with at least one reviewed item in the last 30 days, out of all nonempty decks in this scope. Empty parent folders do not increase the denominator."},
        {label: "Active days", value: Number(stats.activeDays || 0) + " / " + Number(stats.windowDays || 30), color: Theme.ink,
            hint: "Days with at least one recorded answer during the last 30 calendar days, including today."},
        {label: "Recall trend", value: stats.recallChange === null || stats.recallChange === undefined ? "Not yet" : (Number(stats.recallChange) > 0 ? "+" : "") + Number(stats.recallChange).toFixed(1) + " points", color: Theme.ink,
            hint: "Change in observed recall, in percentage points, compared with the previous 30 days. Requires reviews in both periods; the cards reviewed may differ."}
    ]
    function fitScopePopup() {
        const buttonTop = scopeButton.mapToItem(ui.contentItem, 0, 0).y
        const above = Math.max(0, buttonTop - 20)
        scopePopup.height = Math.min(scopePopup.implicitHeight, above)
        scopePopup.y = -scopePopup.height - 8
    }
    function validDeckIds() {
        const available = app.decks.map(deck => String(deck.id))
        return selectedDeckIds.map(id => String(id)).filter((id, index, ids) =>
            available.indexOf(id) >= 0 && ids.indexOf(id) === index)
    }
    function sequenceValues(value) {
        if (value === undefined || value === null || typeof value === "string") return []
        if (typeof value.length !== "number") return []
        const values = []
        for (let index = 0; index < value.length; ++index) values.push(value[index])
        return values
    }
    function reconcileDeckSelection() {
        if (app.busy || !decksSnapshotReady) return
        const valid = validDeckIds()
        if (valid.length !== selectedDeckIds.length)
            preferences.deckIds = valid
        if (!preferences.deckSelectionMigrated) preferences.deckSelectionMigrated = true
    }
    Component.onCompleted: {
        if (!preferences.deckSelectionMigrated && selectedDeckIds.length === 0 && preferences.deckId.length > 0)
            preferences.deckIds = [preferences.deckId]
        if (!app.busy && app.decks.length > 0) decksSnapshotReady = true
        reconcileDeckSelection()
    }
    onActiveChanged: if (!active) scopePopup.close()
    onWidthChanged: if (scopePopup && scopePopup.visible) fitScopePopup()
    onHeightChanged: if (scopePopup && scopePopup.visible) fitScopePopup()
    Connections {
        target: learning.ui
        function onWidthChanged() { if (scopePopup.visible) Qt.callLater(learning.fitScopePopup) }
        function onHeightChanged() { if (scopePopup.visible) Qt.callLater(learning.fitScopePopup) }
    }
    Connections {
        target: app
        function onDecksChanged() {
            learning.decksSnapshotReady = true
            learning.reconcileDeckSelection()
        }
        function onBusyChanged() {
            if (!app.busy && app.decks.length > 0) learning.decksSnapshotReady = true
            learning.reconcileDeckSelection()
        }
    }
    Rectangle {
        anchors.top: parent.top
        width: parent.width
        height: 1
        color: Theme.rule
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 14
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Label {
                text: "Learning"
                color: Theme.ink
                font.family: Theme.contentFont
                font.pixelSize: 22
                font.weight: Font.DemiBold
            }
            Item { Layout.fillWidth: true }
            Label {
                Layout.maximumWidth: Math.max(0, learning.width - 210)
                Layout.minimumWidth: 0
                text: learning.scopeName
                textFormat: Text.PlainText
                color: Theme.inkMuted
                font.family: Theme.uiFont
                font.pixelSize: 12
                elide: Text.ElideRight
                ToolTip.visible: scopeHover.hovered
                ToolTip.text: learning.selectedDeckIds.length > 0 ? "Statistics and activity for " + learning.scopeName + ", including each deck's subdecks" : "Statistics and activity across your entire library"
                HoverHandler { id: scopeHover }
            }
            GlyphButton {
                id: scopeButton
                glyph: "filter"
                hint: "Filter learning by deck"
                selected: scopePopup.visible || learning.selectedDeckIds.length > 0
                onClicked: {
                    if (scopePopup.visible) scopePopup.close()
                    else { learning.fitScopePopup(); scopePopup.open() }
                }
                DeckScopeFilterPopup {
                    id: scopePopup
                    parent: scopeButton
                    width: Math.min(400, learning.width)
                    x: scopeButton.width - width
                    y: -height - 8
                    margins: 12
                    decks: app.decks
                    selectedDeckIds: learning.selectedDeckIds
                    onDeckSelectionChanged: ids => {
                        learning.preferences.deckIds = ids
                        learning.preferences.deckId = ids.length === 1 ? String(ids[0]) : ""
                    }
                    onClosed: if (learning.active) scopeButton.forceActiveFocus()
                }
            }
        }
        Label {
            visible: activity.loading && !learning.hasStats
            Layout.fillWidth: true
            text: "Loading learning history..."
            color: Theme.inkMuted
            font.family: Theme.uiFont
            font.pixelSize: 12
        }
        ScrollView {
            id: learningScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            contentWidth: availableWidth
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical.policy: ScrollBar.AsNeeded
            ColumnLayout {
                width: learningScroll.availableWidth
                spacing: 10
                ErrorBlock {
                    Layout.fillWidth: true
                    visible: learning.hasError
                    code: String(activity.error.code || "")
                    message: String(activity.error.message || "")
                    details: String(activity.error.detail || "")
                }
                AppButton {
                    visible: learning.hasError
                    text: "Retry"
                    enabled: !activity.loading
                    onClicked: activity.reload()
                }
                GridLayout {
                    Layout.fillWidth: true
                    visible: !learning.hasError
                    columns: learning.narrow ? 1 : 2
                    columnSpacing: 26
                    rowSpacing: 18
                    CalendarHeatmap {
                        id: learningCalendar
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.preferredWidth: learning.narrow ? learningScroll.availableWidth : learningScroll.availableWidth * 0.7
                        Layout.maximumWidth: learning.narrow ? learningScroll.availableWidth : learningScroll.availableWidth * 0.7
                        Layout.preferredHeight: learning.narrow ? implicitHeight : Math.max(implicitHeight, learningScroll.availableHeight)
                        spacious: true
                        days: activity.analytics.days || []
                        today: String(activity.analytics.today || "")
                        firstDate: String(activity.analytics.firstDate || "")
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.preferredWidth: learning.narrow ? learningScroll.availableWidth : learningScroll.availableWidth * 0.3 - 26
                        Layout.maximumWidth: learning.narrow ? learningScroll.availableWidth : learningScroll.availableWidth * 0.3 - 26
                        Layout.alignment: Qt.AlignTop
                        spacing: 3
                        Label {
                            Layout.minimumWidth: 0
                            text: "Last 30 days / Provisional"
                            color: Theme.inkMuted
                            font.family: Theme.uiFont
                            font.pixelSize: 12
                            elide: Text.ElideRight
                            Layout.bottomMargin: 5
                        }
                        Repeater {
                            model: learning.measures
                            delegate: Item {
                                id: measure
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredHeight: 32
                                RowLayout {
                                    anchors.fill: parent
                                    spacing: 10
                                    Label {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        text: measure.modelData.label
                                        color: Theme.inkMuted
                                        font.family: Theme.uiFont
                                        font.pixelSize: 12
                                        elide: Text.ElideRight
                                        ToolTip.visible: measureLabelHover.hovered
                                        ToolTip.text: measure.modelData.label
                                        HoverHandler { id: measureLabelHover }
                                    }
                                    Label {
                                        Layout.minimumWidth: 0
                                        text: learning.hasStats ? measure.modelData.value : "-"
                                        color: measure.modelData.color
                                        font.family: Theme.monoFont
                                        font.pixelSize: 14
                                        font.weight: Font.DemiBold
                                    }
                                }
                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    width: parent.width
                                    height: 1
                                    color: Theme.rule
                                    opacity: 0.5
                                }
                                ToolTip.visible: measureHover.hovered
                                ToolTip.text: modelData.hint
                                HoverHandler { id: measureHover }
                                Accessible.role: Accessible.StaticText
                                Accessible.name: modelData.label + ": " + (learning.hasStats ? modelData.value : "Loading")
                                Accessible.description: modelData.hint
                            }
                        }
                    }
                }
            }
        }
    }
}
