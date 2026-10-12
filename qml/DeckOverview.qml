pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import "DeckStudyData.js" as Study

Item {
    id: overview
    required property var ui
    readonly property bool descriptionEditing: details.descriptionEditing
    readonly property var decks: Study.fromCollection(app.decks, app.cards, app.history)
    readonly property var selectedDeck: decks.find(d => d.id === app.selectedDeckId) || ({})
    readonly property bool narrow: width < 720
    readonly property bool paneOpen: !!selectedDeck.id && preferences.detailsOpen
    readonly property var scopeIds: {
        const ids = [app.selectedDeckId];
        for (let i = 0; i < ids.length; ++i)
            app.decks.filter(d => d.parentId === ids[i] && ids.indexOf(d.id) < 0).forEach(d => ids.push(d.id));
        return ids;
    }
    readonly property string summaryKey: Qt.md5(JSON.stringify([
        app.selectedDeckId, ai.endpoint, ai.model,
        app.decks.filter(d => scopeIds.indexOf(d.id) >= 0).map(d => [d.id, d.name]),
        app.cards.filter(c => scopeIds.indexOf(c.deckId) >= 0)
    ]))
    readonly property bool canSummarize: visible && paneOpen && !app.reviewing && !app.busy
        && !ui.modalOpen && !descriptionEditing && (selectedDeck.due || 0) > 0 && ai.hasApiKey
    property var summaryCache: ({})
    property string pendingSummaryKey: ""
    property string pendingSummaryDeck: ""
    readonly property var summaryResult: summaryCache[summaryKey] || ({})
    property string savingDescriptionId: ""
    property string savingDescription: ""
    property Settings preferences: Settings {
        category: "deckOverview"
        property bool detailsOpen: true
        property real paneWidth: 360
    }
    onSummaryKeyChanged: scheduleSummary()
    onCanSummarizeChanged: scheduleSummary()
    onVisibleChanged: if (!visible && graphDeckMenu) graphDeckMenu.close()
    Component.onCompleted: scheduleSummary()

    function cancelOwnedSummary() {
        if (!pendingSummaryKey) return;
        const ownedDeck = pendingSummaryDeck;
        pendingSummaryKey = "";
        pendingSummaryDeck = "";
        if (ai.busy && ai.summaryDeckId === ownedDeck) ai.cancel();
    }
    function scheduleSummary() {
        if (!summaryDelay) return;
        summaryDelay.stop();
        if (!canSummarize || pendingSummaryKey && pendingSummaryKey !== summaryKey)
            cancelOwnedSummary();
        if (canSummarize && !summaryCache[summaryKey] && !ai.busy)
            summaryDelay.restart();
    }
    function retrySummary() {
        const cache = Object.assign({}, summaryCache);
        delete cache[summaryKey];
        summaryCache = cache;
        scheduleSummary();
    }
    function saveDescription(id, text) {
        if (app.busy) return;
        const deck = app.decks.find(d => d.id === id);
        if (!deck) {
            ui.showError("DECK_NOT_FOUND", "This deck no longer exists. Choose another deck.", "");
            return;
        }
        savingDescriptionId = id;
        savingDescription = text;
        app.updateDeck(id, deck.name, text, deck.parentId || "");
    }
    function finishDescriptionSave() {
        if (app.busy || !savingDescriptionId) return;
        const deck = app.decks.find(d => d.id === savingDescriptionId);
        if (!app.lastError.code && deck && deck.description === savingDescription)
            details.descriptionSaveSucceeded(savingDescriptionId, savingDescription);
        savingDescriptionId = "";
        savingDescription = "";
    }
    function openGraphDeckMenu(deckId, menuPoint) {
        if (!deckId || !Overlay.overlay)
            return;
        graphDeckMenu.deckId = deckId;
        const overlayPoint = graph.mapToItem(Overlay.overlay, menuPoint.x, menuPoint.y);
        graphDeckMenu.x = overlayPoint.x;
        graphDeckMenu.y = overlayPoint.y;
        graphDeckMenu.open();
    }
    Timer {
        id: summaryDelay
        interval: 700
        onTriggered: {
            if (!overview.canSummarize || ai.busy || overview.summaryCache[overview.summaryKey]) return;
            overview.pendingSummaryKey = overview.summaryKey;
            overview.pendingSummaryDeck = app.selectedDeckId;
            ai.summarizeDeck(app.selectedDeckId);
        }
    }
    Connections {
        target: ai
        function onChanged() {
            if (!ai.busy && overview.pendingSummaryKey) {
                if (ai.summaryDeckId === overview.pendingSummaryDeck && (ai.summary || ai.error)) {
                    const cache = Object.assign({}, overview.summaryCache);
                    cache[overview.pendingSummaryKey] = {text: ai.summary, error: ai.error, status: ai.status};
                    const keys = Object.keys(cache);
                    keys.slice(0, Math.max(0, keys.length - 12)).forEach(key => delete cache[key]);
                    overview.summaryCache = cache;
                }
                overview.pendingSummaryKey = "";
                overview.pendingSummaryDeck = "";
            }
            overview.scheduleSummary();
        }
        function onKeyChanged() {
            overview.cancelOwnedSummary();
            overview.summaryCache = ({});
            overview.scheduleSummary();
        }
    }
    Connections {
        target: app
        function onSelectedDeckIdChanged() {
            overview.preferences.detailsOpen = true;
            graph.revealSelection();
        }
        function onBusyChanged() { Qt.callLater(overview.finishDescriptionSave); }
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 18
            Layout.preferredHeight: 54
            Item { Layout.fillWidth: true }
            AppButton {
                text: "New root deck"
                primary: app.decks.length === 0
                enabled: !app.busy
                hint: "Create a top-level deck"
                onClicked: overview.ui.openDeckEditor("", "")
            }
            GlyphButton {
                glyph: "more"
                hint: "Selected deck actions"
                enabled: !!overview.selectedDeck.id && !app.busy
                onClicked: deckMenu.open()
                AppMenu {
                    id: deckMenu
                    y: parent.height
                    AppMenuItem { glyph: "folder"
                        text: "New child deck"
                        onTriggered: overview.ui.openDeckEditor("", app.selectedDeckId)
                    }
                    AppMenuItem { glyph: "edit"
                        text: "Edit deck"
                        onTriggered: overview.ui.openDeckEditor(app.selectedDeckId)
                    }
                    AppMenuItem { glyph: "plus"
                        text: "Add card"
                        onTriggered: overview.ui.openCardEditor("")
                    }
                    AppMenuItem { glyph: "empty-box"
                        text: "Reset deck"
                        visible: app.developmentToolsEnabled
                        height: visible ? implicitHeight : 0
                        onTriggered: overview.ui.confirmReset("deck", app.selectedDeckId, overview.selectedDeck.name)
                    }
                }
            }
        }
        RowLayout {
            visible: app.decks.length > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            DeckHierarchyStudy {
                id: graph
                visible: !overview.narrow || !overview.paneOpen
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 0
                variant: 1
                decks: overview.decks
                selectedId: app.selectedDeckId
                detailsVisible: overview.paneOpen
                interactionBusy: app.busy || app.reviewing
                onDeckSelected: id => {
                    overview.ui.setDeck(id, true);
                    overview.preferences.detailsOpen = true;
                }
                onReviewRequested: id => {
                    overview.cancelOwnedSummary();
                    if (!graph.interactionBusy && id) {
                        overview.ui.setDeck(id, true);
                        app.startReview(id);
                    }
                }
                onContextMenuRequested: (id, menuPoint) => overview.openGraphDeckMenu(id, menuPoint)
                onShowDetailsRequested: overview.preferences.detailsOpen = true
            }
            DeckActionsMenu {
                id: graphDeckMenu
                parent: Overlay.overlay
                ui: overview.ui
                deckId: ""
                onBeforeAction: { overview.cancelOwnedSummary(); graphDeckMenu.close() }
            }
            Item {
                id: resizeHandle
                visible: overview.paneOpen && !overview.narrow
                Layout.preferredWidth: 10
                Layout.fillHeight: true
                activeFocusOnTab: true
                Accessible.name: "Resize deck details. Use Left or Right; Home restores the default width."
                Keys.onLeftPressed: overview.preferences.paneWidth = Math.min(overview.width * 0.48, details.width + 24)
                Keys.onRightPressed: overview.preferences.paneWidth = Math.max(300, details.width - 24)
                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Home) { overview.preferences.paneWidth = 360; event.accepted = true; }
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: 2
                    height: 44
                    color: resizeHandle.activeFocus ? Theme.accent : Theme.rule
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.SizeHorCursor
                    property real pressX: 0
                    property real pressWidth: 0
                    onPressed: mouse => {
                        resizeHandle.forceActiveFocus();
                        pressX = mapToItem(overview, mouse.x, mouse.y).x;
                        pressWidth = details.width;
                    }
                    onPositionChanged: mouse => {
                        if (pressed)
                            overview.preferences.paneWidth = Math.max(300, Math.min(overview.width * 0.48,
                                pressWidth - (mapToItem(overview, mouse.x, mouse.y).x - pressX)));
                    }
                }
            }
            DeckStudyDetails {
                id: details
                visible: overview.paneOpen
                Layout.fillWidth: overview.narrow
                Layout.preferredWidth: overview.narrow ? overview.width : Math.max(300, Math.min(overview.preferences.paneWidth, overview.width * 0.48))
                Layout.fillHeight: true
                deck: overview.selectedDeck
                mediaRoot: media.rootPath
                busy: app.busy
                actionText: (overview.selectedDeck.total || 0) === 0 ? "Add card" : "Start review"
                actionEnabled: !!overview.selectedDeck.id && ((overview.selectedDeck.total || 0) === 0 || (overview.selectedDeck.due || 0) > 0)
                saveExternally: true
                summaryText: overview.summaryResult.text || (!ai.hasApiKey
                    ? "Add your provider key in Settings / Language model to summarize the due cards."
                    : overview.summaryResult.error ? "" : "A summary of the due cards will appear here.")
                summaryCaption: "Automatic summary"
                summaryBusy: ai.busy && overview.pendingSummaryKey === overview.summaryKey
                summaryError: overview.summaryResult.error || ""
                summaryRetryVisible: ai.hasApiKey
                onSummaryRetryRequested: overview.retrySummary()
                onDescriptionSaved: (id, text) => overview.saveDescription(id, text)
                onCollapseRequested: overview.preferences.detailsOpen = false
                onReviewRequested: id => {
                    overview.cancelOwnedSummary();
                    if ((overview.selectedDeck.total || 0) === 0)
                        overview.ui.openCardEditor("");
                    else
                        app.startReview(id);
                }
            }
        }
        Label {
            visible: app.decks.length === 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 24
            text: app.busy ? "Loading decks..." : "Your deck trees will appear here. Create a deck to begin."
            color: Theme.inkMuted
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.Wrap
        }
    }
}
