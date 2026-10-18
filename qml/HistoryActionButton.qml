import QtQuick
import QtQuick.Controls

Item {
    id: historyControl
    required property var ui
    required property string action
    implicitWidth: 38
    implicitHeight: 38
    readonly property bool available: ui.historyActionAllowed(action)
    readonly property string hint: ui.historyActionHint(action)
    GlyphButton {
        id: historyButton
        anchors.fill: parent
        glyph: historyControl.action
        hint: historyControl.hint
        enabled: historyControl.available
        opacity: enabled ? 1 : 0.4
        ToolTip.visible: false
        onClicked: historyControl.ui.runAction(historyControl.action)
    }
    HoverHandler { id: historyHover }
    ToolTip.visible: historyHover.hovered || historyButton.activeFocus
    ToolTip.text: hint
}
