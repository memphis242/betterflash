import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Flickable {
    id: root
    property bool correctionEnabled: true
    property real nodeHeight: 68
    property real arrowWidth: 18
    property real flowSpacing: 6

    Layout.fillWidth: true
    Layout.minimumWidth: 0
    clip: true
    interactive: contentWidth > width
    flickableDirection: Flickable.HorizontalFlick
    boundsBehavior: Flickable.StopAtBounds
    contentWidth: flow.implicitWidth
    contentHeight: flow.implicitHeight
    implicitWidth: flow.implicitWidth
    implicitHeight: flow.implicitHeight + (interactive ? 10 : 0)
    activeFocusOnTab: interactive
    Keys.onLeftPressed: contentX = Math.max(0, contentX - 96)
    Keys.onRightPressed: contentX = Math.min(Math.max(0, contentWidth - width), contentX + 96)
    Keys.onPressed: event => {
        switch (event.key) {
        case Qt.Key_Home:
            contentX = 0
            event.accepted = true
            break
        case Qt.Key_End:
            contentX = Math.max(0, contentWidth - width)
            event.accepted = true
            break
        }
    }
    ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

    component FlowNode: Rectangle {
        id: node
        required property string title
        required property string description
        required property string iconKind
        property bool muted: false
        property bool highlighted: false
        property real nodeWidth: 68
        width: nodeWidth
        height: root.nodeHeight
        radius: 6
        color: Theme.surfaceRaised
        border.width: 1
        border.color: highlighted && !muted ? Theme.accent : Theme.rule

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 7
            spacing: 3
            Icon {
                kind: node.iconKind
                Layout.preferredWidth: 21
                Layout.preferredHeight: 21
                Layout.alignment: Qt.AlignHCenter
                stroke: node.muted ? Theme.inkMuted : Theme.ink
                lineWidth: 1.35
            }
            Label {
                text: node.title
                color: node.muted ? Theme.inkMuted : Theme.ink
                font.family: Theme.monoFont
                font.pixelSize: 11
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                Layout.fillHeight: true
                verticalAlignment: Text.AlignVCenter
            }
        }
        ToolTip.visible: hoverHandler.hovered
        ToolTip.text: node.description
        HoverHandler { id: hoverHandler }
    }

    component FlowArrow: Item {
        width: root.arrowWidth
        height: root.nodeHeight
        Icon {
            anchors.centerIn: parent
            width: 18
            height: 18
            kind: "arrow-right"
            stroke: Theme.inkMuted
            lineWidth: 1.35
        }
    }

    Row {
        id: flow
        spacing: root.flowSpacing

        FlowNode {
            title: "Speech"
            description: "Words spoken by the user"
            iconKind: "mic"
        }
        FlowArrow {}
        FlowNode {
            title: "Recognition"
            description: "The speech recognition model turns audio into text"
            iconKind: "ear"
            nodeWidth: 94
        }
        FlowArrow {}
        FlowNode {
            title: "LLM"
            description: root.correctionEnabled
                ? "LLM means language model. The selected local or cloud model can correct grammar and likely technical terms"
                : "LLM means language model. Correction is off, so recognized text is used directly"
            iconKind: "paper-pencil"
            muted: !root.correctionEnabled
            highlighted: true
            nodeWidth: 66
        }
        FlowArrow {}
        FlowNode {
            title: "Text"
            description: "The text used by voice review"
            iconKind: "review-ledger"
        }
    }
}
