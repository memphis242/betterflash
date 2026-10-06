import QtQuick
import QtQuick.Controls
import "ReviewControlDesigns.js" as Controls

FocusScope {
    id: control
    signal clicked()
    readonly property bool hovered: button.hovered
    readonly property bool down: button.down
    property int designIndex: 0
    property string action: "defer"
    property bool demonstrateHover: false
    readonly property var design: Controls.get(designIndex)
    readonly property string glyph: design.glyphs[action] || design.glyphs.defer
    readonly property string accessibleName: action === "return" ? "Center the active card in the review queue" : action === "postpone" ? "Review this card on a later date" : "Defer this card to the end of the review queue"
    readonly property string hint: accessibleName + (action === "defer" ? " (D)" : action === "postpone" ? " (S)" : "")
    readonly property bool highlighted: enabled && (hovered || demonstrateHover || down)
    readonly property bool plain: design.buttonStyle === "plain"
    readonly property bool grouped: design.buttonStyle === "capsule"
    readonly property bool square: design.buttonStyle === "square"
    implicitWidth: action === "return" ? design.returnWidth : design.size
    implicitHeight: action === "return" ? design.returnHeight : design.size
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: accessibleName
    Accessible.onPressAction: {
        if (enabled)
            clicked()
    }
    ToolTip.visible: hovered || activeFocus
    ToolTip.text: hint
    ToolButton {
        id: button
        anchors.fill: parent
        enabled: control.enabled
        focus: true
        focusPolicy: Qt.StrongFocus
        activeFocusOnTab: false
        hoverEnabled: true
        padding: 0
        Accessible.ignored: true
        onClicked: {
            if (control.enabled)
                control.clicked()
        }
        Keys.onReturnPressed: {
            if (control.enabled)
                control.clicked()
        }
        Keys.onEnterPressed: {
            if (control.enabled)
                control.clicked()
        }
        contentItem: Item {
            Icon {
                objectName: "reviewControlGlyph"
                anchors.centerIn: parent
                width: Math.min(control.design.iconSize, control.height - 10)
                height: width
                kind: control.glyph
                stroke: !control.enabled ? Theme.inkMuted : (control.highlighted && !control.square || control.activeFocus) ? Theme.accent : Theme.ink
            }
        }
        background: Item {
            Rectangle {
                anchors.fill: parent
                anchors.margins: control.grouped ? 3 : 0
                radius: control.design.buttonStyle === "circle" ? height / 2 : control.design.radius
                color: !control.enabled ? Theme.transparent
                     : control.design.buttonStyle === "raised" ? (control.highlighted ? Theme.accentSoft : Theme.surfaceRaised)
                     : control.grouped ? (control.highlighted ? Theme.surfaceRaised : Theme.transparent)
                     : control.plain || control.square ? Theme.transparent
                     : control.highlighted ? Theme.accentSoft : Theme.transparent
                border.width: control.plain ? 0 : control.grouped ? (control.highlighted ? 1 : 0) : control.activeFocus || control.square && control.highlighted ? 2 : 1
                border.color: control.activeFocus || control.highlighted && !control.grouped ? Theme.accent : Theme.rule
                opacity: control.enabled ? 1 : 0.55
            }
            Rectangle {
                visible: control.plain && (control.highlighted || control.activeFocus)
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 2
                width: control.design.iconSize - 2
                height: control.activeFocus ? 2 : 1
                radius: 0.5
                color: Theme.accent
            }
            Rectangle {
                visible: control.activeFocus && (control.plain || control.grouped)
                anchors.fill: parent
                radius: control.design.radius
                color: Theme.transparent
                border.color: Theme.accent
                border.width: 1
            }
        }
    }
}
