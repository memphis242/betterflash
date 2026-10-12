pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import "DeckStudyData.js" as Study

ApplicationWindow {
    id: gallery
    width: 1440
    height: 940
    minimumWidth: 1100
    minimumHeight: 780
    visible: true
    title: "BetterFlash - Deck hierarchy studies"
    color: Theme.canvas
    font.family: Theme.uiFont
    font.pixelSize: 14
    palette.window: Theme.canvas
    palette.windowText: Theme.ink
    palette.text: Theme.ink
    palette.base: Theme.surface
    palette.button: Theme.surface
    palette.buttonText: Theme.ink
    palette.highlight: Theme.accentSoft
    palette.highlightedText: Theme.ink
    palette.placeholderText: Theme.inkMuted
    palette.toolTipBase: Theme.surfaceRaised
    palette.toolTipText: Theme.ink
    property int variant: deckStudyStartVariant
    property bool useCollection: deckStudyApp !== null
    property string selectedId: ""
    property var paneStates: [true, true]
    property var descriptions: ({})
    readonly property var sampleDecks: Study.makeDecks()
    readonly property var sourceDecks: useCollection && deckStudyApp
        ? Study.fromCollection(deckStudyApp.decks, deckStudyApp.cards, deckStudyApp.history, true) : sampleDecks
    readonly property var decks: sourceDecks.map(d => Object.assign({}, d, {description: descriptions[d.id] !== undefined ? descriptions[d.id] : d.description}))
    readonly property var selectedDeck: decks.find(d => d.id === selectedId) || ({})
    readonly property bool paneOpen: !!selectedDeck.id && paneStates[variant]
    readonly property var names: ["01  Canopy", "02  Branch ledger"]
    property Settings preferences: Settings {
        category: "deckGraphStudies"
        property real paneWidth: 360
    }
    function setPaneOpen(value) {
        const states = paneStates.slice();
        states[variant] = value;
        paneStates = states;
    }
    function chooseDeck(id) {
        selectedId = id;
        setPaneOpen(true);
    }
    onSourceDecksChanged: {
        if (!sourceDecks.some(d => d.id === selectedId)) {
            const preferred = sourceDecks.find(d => d.name === "BetterFlash examples") || sourceDecks.find(d => !d.parent);
            selectedId = preferred ? preferred.id : "";
        }
    }
    Component.onCompleted: {
        if (!Theme.hasChoice) {
            Theme.preferences.savedDark = true;
            Theme.preferences.hasThemeChoice = true;
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 72
            color: Theme.surface
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 24
                anchors.rightMargin: 20
                spacing: 12
                BrandMark { Layout.preferredWidth: 32; Layout.preferredHeight: 32; Layout.rightMargin: 12 }
                Repeater {
                    model: gallery.names
                    delegate: AppButton {
                        required property string modelData
                        required property int index
                        text: modelData
                        primary: gallery.variant === index
                        onClicked: gallery.variant = index
                        hint: "Open the " + modelData.slice(4) + " deck hierarchy study"
                    }
                }
                Item { Layout.fillWidth: true }
                AppButton {
                    visible: !!deckStudyApp
                    text: gallery.useCollection ? "Working decks" : "Sample decks"
                    hint: gallery.useCollection ? "Switch to the sample collection with populated subdecks" : "Switch to the working collection"
                    onClicked: gallery.useCollection = !gallery.useCollection
                }
                GlyphButton {
                    glyph: Theme.dark ? "sun" : "moon"
                    hint: Theme.dark ? "Switch to light theme" : "Switch to dark theme"
                    onClicked: Theme.toggle()
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.rule }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 500
                currentIndex: gallery.variant
                Repeater {
                    model: gallery.names.length
                    delegate: DeckHierarchyStudy {
                        required property int index
                        decks: gallery.decks
                        variant: index
                        selectedId: gallery.selectedId
                        detailsVisible: gallery.paneOpen
                        onDeckSelected: id => gallery.chooseDeck(id)
                        onShowDetailsRequested: gallery.setPaneOpen(true)
                    }
                }
            }
            Item {
                id: resizeHandle
                visible: gallery.paneOpen
                Layout.preferredWidth: 10
                Layout.fillHeight: true
                activeFocusOnTab: true
                Accessible.name: "Resize deck details. Left and right arrows adjust the width; Home restores the default."
                Keys.onLeftPressed: gallery.preferences.paneWidth = Math.min(gallery.width * 0.45, details.width + 24)
                Keys.onRightPressed: gallery.preferences.paneWidth = Math.max(320, details.width - 24)
                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Home) {gallery.preferences.paneWidth = 360; event.accepted = true;}
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
                        resizeHandle.forceActiveFocus()
                        pressX = mapToItem(gallery.contentItem, mouse.x, mouse.y).x
                        pressWidth = details.width
                    }
                    onPositionChanged: mouse => {
                        if (pressed)
                            gallery.preferences.paneWidth = Math.max(320, Math.min(gallery.width * 0.45, pressWidth - (mapToItem(gallery.contentItem, mouse.x, mouse.y).x - pressX)))
                    }
                }
            }
            DeckStudyDetails {
                mediaRoot: deckStudyApp ? deckStudyApp.mediaPath : ""
                id: details
                visible: gallery.paneOpen
                Layout.preferredWidth: Math.max(320, Math.min(gallery.preferences.paneWidth, gallery.width * 0.45))
                Layout.fillHeight: true
                deck: gallery.selectedDeck
                variant: gallery.variant
                onCollapseRequested: gallery.setPaneOpen(false)
                onDescriptionSaved: (id, description) => {
                    const changed = Object.assign({}, gallery.descriptions);
                    changed[id] = description;
                    gallery.descriptions = changed;
                }
                onReviewRequested: id => previewReviewDialog.open()
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            color: Theme.surface
            Rectangle { width: parent.width; height: 1; color: Theme.rule }
            Label {
                anchors.fill: parent
                anchors.leftMargin: 24
                anchors.rightMargin: 24
                verticalAlignment: Text.AlignVCenter
                text: gallery.useCollection
                    ? "Design preview · working decks and recent review stats · sample LLM text · description edits stay in this window"
                    : "Design preview · sample decks, review stats and LLM text · description edits stay in this window"
                color: Theme.inkMuted
                font.pixelSize: 11
                elide: Text.ElideRight
            }
        }
    }
    Dialog {
        id: previewReviewDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(480, gallery.width - 48)
        padding: 24
        title: "Review preview"
        standardButtons: Dialog.Close
        background: Rectangle { color: Theme.surface; radius: 8; border.color: Theme.rule }
        Overlay.modal: Rectangle { color: Theme.scrim }
        Label {
            width: parent.width
            text: "This layout would start a review of \"" + gallery.selectedDeck.name + "\" and its subdecks. Continue reviewing cards in the main BetterFlash window."
            textFormat: Text.PlainText
            color: Theme.ink
            wrapMode: Text.Wrap
        }
    }
    Connections {
        target: deckStudyApp
        function onLastErrorChanged() {
            if (deckStudyApp.lastError.code) collectionError.open()
        }
    }
    Dialog {
        id: collectionError
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(540, gallery.width - 48)
        title: "Collection unavailable"
        standardButtons: Dialog.Close
        background: Rectangle { color: Theme.surface; radius: 8; border.color: Theme.rule }
        contentItem: ErrorBlock {
            code: deckStudyApp ? deckStudyApp.lastError.code || "" : ""
            message: deckStudyApp ? deckStudyApp.lastError.message || "" : ""
            details: deckStudyApp ? deckStudyApp.lastError.detail || "" : ""
        }
    }
}
