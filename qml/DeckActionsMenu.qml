import QtQuick
import QtQuick.Controls

AppMenu {
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

    AppMenuItem { glyph: "plus"
        visible: menu.headerMode
        height: visible ? implicitHeight : 0
        text: "New deck"
        enabled: !app.busy
        onTriggered: { menu.beforeAction(); menu.ui.openDeckEditor("") }
    }
    AppMenuSeparator { visible: menu.headerMode }

    AppMenuItem { glyph: "plus"
        text: "Add card"
        enabled: menu.deckAvailable && !app.busy
        onTriggered: { menu.beforeAction(); menu.selectDeck(true); menu.ui.openCardEditor("") }
    }
    AppMenuItem { glyph: "folder"
        text: "Add subdeck"
        enabled: menu.deckAvailable && !app.busy
        onTriggered: { menu.beforeAction(); menu.ui.openDeckEditor("", menu.deckId) }
    }
    AppMenuItem { glyph: "edit"
        text: "Rename deck"
        enabled: menu.deckAvailable && !app.busy
        onTriggered: { menu.beforeAction(); menu.ui.openDeckEditor(menu.deckId) }
    }
    AppMenuSeparator {}
    AppMenuItem { glyph: "review-flip"
        text: "Start review"
        enabled: menu.deckAvailable && !app.busy
        onTriggered: {
            menu.beforeAction()
            menu.selectDeck(false)
            menu.ui.page = 0
            app.startReview(menu.deckId)
        }
    }
    AppMenuItem { glyph: "calendar-page"
        text: "Heatmap"
        enabled: menu.deckAvailable
        onTriggered: { menu.beforeAction(); menu.ui.openDeckHeatmap(menu.deckId) }
    }
    AppMenuItem { glyph: "empty-box"
        text: "Reset deck state"
        visible: app.developmentToolsEnabled
        height: visible ? implicitHeight : 0
        enabled: menu.deckAvailable && !app.busy
        onTriggered: { menu.beforeAction(); menu.ui.confirmReset("deck", menu.deckId, menu.deck.name) }
    }
    AppMenuSeparator {}
    AppMenuItem { glyph: "pin"
        text: "Checkpoint"
        enabled: menu.deckAvailable && !app.busy
        onTriggered: { menu.beforeAction(); menu.ui.openDeckCheckpoint(menu.deckId, false) }
    }
    AppMenuItem { glyph: "clock"
        text: "Restore to known checkpoint"
        enabled: menu.deckAvailable && !app.busy
        onTriggered: { menu.beforeAction(); menu.ui.openDeckCheckpoint(menu.deckId, true) }
    }
    AppMenuSeparator {}
    AppMenuItem { glyph: "close"; danger: true
        text: "Delete deck"
        enabled: menu.deckAvailable && !app.busy
        onTriggered: { menu.beforeAction(); menu.ui.confirmDelete("deck", menu.deckId, menu.deck.name) }
    }
}
