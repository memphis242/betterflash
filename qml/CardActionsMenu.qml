import QtQuick
import QtQuick.Controls

Menu {
    id: menu
    required property var ui
    property string cardId: ""
    signal beforeAction()

    readonly property var card: ui.cardById(cardId)
    readonly property bool cardAvailable: !!card && !!card.id

    function cardName() {
        return ui.compactText(card.front || "").slice(0, 80)
    }

    MenuItem {
        text: "View card"
        enabled: menu.cardAvailable
        onTriggered: {
            menu.beforeAction()
            menu.ui.selectedCardId = menu.cardId
            menu.ui.openSelectedDetail()
        }
    }
    MenuItem {
        text: "Edit card"
        enabled: menu.cardAvailable
        onTriggered: {
            menu.beforeAction()
            menu.ui.openCardEditor(menu.cardId)
        }
    }
    MenuSeparator {}
    MenuItem {
        text: "Move card"
        enabled: menu.cardAvailable && !app.busy
        onTriggered: {
            menu.beforeAction()
            menu.ui.openMoveCard(menu.cardId)
        }
    }
    MenuItem {
        text: "Delete card"
        enabled: menu.cardAvailable
        onTriggered: {
            menu.beforeAction()
            menu.ui.confirmDelete("card", menu.cardId, menu.cardName())
        }
    }
    MenuItem {
        text: "Atomicize card"
        enabled: menu.cardAvailable
        ToolTip.visible: hovered
        ToolTip.text: "Preview proposed focused cards split from distinct ideas before applying them."
        onTriggered: {
            menu.beforeAction()
            menu.ui.openAtomicize(menu.cardId)
        }
    }
    MenuItem {
        text: "Reset card"
        visible: app.developmentToolsEnabled
        height: visible ? implicitHeight : 0
        enabled: menu.cardAvailable && !app.busy
        ToolTip.visible: hovered
        ToolTip.text: "Reset scheduling, difficulty and review counts without deleting the card. Retained review history is preserved."
        onTriggered: {
            menu.beforeAction()
            menu.ui.confirmReset("card", menu.cardId, menu.cardName())
        }
    }
}
