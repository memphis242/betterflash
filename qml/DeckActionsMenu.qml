import QtQuick
import QtQuick.Controls

Menu {
    id: menu
    required property var ui
    property string deckId: ""
    property bool headerMode: false
    signal beforeAction()

    readonly property var deck: ui.deckById(deckId)
    readonly property bool deckAvailable: !!deck && !!deck.id

    function selectDeck(stayOnPage) {
        if (menu.deckAvailable)
            menu.ui.setDeck(menu.deckId, stayOnPage)
    }

    MenuItem {
        visible: menu.headerMode
        height: visible ? implicitHeight : 0
        text: "New deck"
        enabled: !app.busy
        onTriggered: { menu.beforeAction(); menu.ui.openDeckEditor("") }
    }
    MenuSeparator { visible: menu.headerMode }

    MenuItem {
        text: "Add card"
        enabled: menu.deckAvailable && !app.busy
        onTriggered: { menu.beforeAction(); menu.selectDeck(true); menu.ui.openCardEditor("") }
    }
    MenuItem {
        text: "Add subdeck"
        enabled: menu.deckAvailable && !app.busy
        onTriggered: { menu.beforeAction(); menu.ui.openDeckEditor("", menu.deckId) }
    }
    MenuItem {
        text: "Rename deck"
        enabled: menu.deckAvailable && !app.busy
        onTriggered: { menu.beforeAction(); menu.ui.openDeckEditor(menu.deckId) }
    }
    MenuItem {
        text: "Start review"
        enabled: menu.deckAvailable && !app.busy
        onTriggered: {
            menu.beforeAction()
            menu.selectDeck(false)
            menu.ui.page = 0
            app.startReview(menu.deckId)
        }
    }
    MenuItem {
        text: "Reset deck state"
        visible: app.developmentToolsEnabled
        height: visible ? implicitHeight : 0
        enabled: menu.deckAvailable && !app.busy
        onTriggered: { menu.beforeAction(); menu.ui.confirmReset("deck", menu.deckId, menu.deck.name) }
    }
    MenuSeparator {}
    MenuItem {
        text: "Checkpoint"
        enabled: menu.deckAvailable
        ToolTip.visible: hovered
        ToolTip.text: "Deck checkpoints are planned and inactive for now."
        onTriggered: {}
    }
    MenuItem {
        text: "Restore to known checkpoint"
        enabled: menu.deckAvailable
        ToolTip.visible: hovered
        ToolTip.text: "Checkpoint restoration is planned and inactive for now."
        onTriggered: {}
    }
    MenuSeparator {}
    MenuItem {
        text: "Delete deck"
        enabled: menu.deckAvailable && !app.busy
        onTriggered: { menu.beforeAction(); menu.ui.confirmDelete("deck", menu.deckId, menu.deck.name) }
    }
}
