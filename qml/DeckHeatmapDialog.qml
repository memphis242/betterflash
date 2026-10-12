import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog

    required property var ui
    property string deckId: ""
    readonly property string deckName: (ui.deckById(deckId) || {}).name || "Deck"

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(1100, ui.width - 2 * ui.gutter)
    height: Math.min(400, ui.height - 2 * ui.gutter)
    padding: ui.width < 600 ? 14 : 22
    modal: true
    closePolicy: Popup.CloseOnEscape
    background: Rectangle {
        color: Theme.surface
        radius: 16
        border.color: Theme.ledgerRule
    }
    Overlay.modal: Rectangle {
        color: Theme.scrim
    }

    function openForDeck(id) {
        deckId = String(id || "");
        if (deckId)
            open();
    }

    onOpened: {
        calendar.goToToday();
        calendar.forceActiveFocus();
    }

    StudyActivityModel {
        id: activity
        deckId: dialog.deckId
        active: dialog.visible
    }

    header: Item {
        implicitHeight: 76
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: dialog.padding
            anchors.rightMargin: dialog.padding
            anchors.topMargin: 16
            anchors.bottomMargin: 12
            spacing: 12
            Label {
                Layout.fillWidth: true
                text: dialog.deckName + " - Heatmap"
                textFormat: Text.PlainText
                elide: Text.ElideRight
                color: Theme.ink
                font.family: Theme.uiFont
                font.pixelSize: 22
                font.weight: Font.DemiBold
                ToolTip.visible: titleHover.hovered
                ToolTip.text: "Review activity for " + dialog.deckName + " and its child decks"
                HoverHandler {
                    id: titleHover
                }
            }
            GlyphButton {
                glyph: "close"
                hint: "Close heatmap"
                onClicked: dialog.close()
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 8
        Label {
            visible: activity.loading && !activity.analytics.today
            text: "Loading review activity"
            color: Theme.inkMuted
            Layout.fillWidth: true
        }
        ScrollView {
            id: errorViewport
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !!activity.error.code
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                width: errorViewport.availableWidth
                spacing: 12
                ErrorBlock {
                    Layout.fillWidth: true
                    code: activity.error.code || ""
                    message: activity.error.message || ""
                    details: activity.error.detail || ""
                }
                AppButton {
                    text: "Retry"
                    enabled: !activity.loading
                    onClicked: activity.reload()
                }
            }
        }
        CalendarHeatmap {
            id: calendar
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !activity.error.code
            days: activity.analytics.days || []
            today: activity.analytics.today || ""
            firstDate: activity.analytics.firstDate || ""
        }
    }
}
