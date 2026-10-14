import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Flickable {
    id: root
    property bool correctionEnabled: true
    property int styleIndex: 2
    property bool spacious: false
    property real nodeHeight: styleIndex === 3 ? (spacious ? 92 : 72) : (spacious ? 96 : 78)
    property real flowSpacing: spacious ? (styleIndex === 2 ? 24 : 12) : (styleIndex === 2 ? 14 : 6)
    property real diagramPadding: styleIndex === 1 || styleIndex === 4 ? (spacious ? 20 : 12) : 4
    readonly property real naturalWidth: flow.implicitWidth + diagramPadding * 2

    Layout.fillWidth: true
    Layout.minimumWidth: 0
    clip: true
    interactive: contentWidth > width
    flickableDirection: Flickable.HorizontalFlick
    boundsBehavior: Flickable.StopAtBounds
    contentWidth: Math.max(width, naturalWidth)
    contentHeight: flow.implicitHeight + diagramPadding * 2
        + (styleIndex === 2 ? 32 : styleIndex === 4 ? 26 : 0)
    implicitWidth: naturalWidth
    implicitHeight: contentHeight + (interactive ? 10 : 0)
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

    component FlowNode: Item {
        id: node
        required property string title
        required property string description
        required property string iconKind
        required property string detail
        property bool muted: false
        property bool highlighted: false
        property real nodeWidth: 92
        width: nodeWidth
        height: root.nodeHeight

        Icon {
            id: nodeIcon
            kind: node.iconKind
            width: root.styleIndex === 3 ? 24 : 30
            height: width
            anchors.top: root.styleIndex === 3 ? parent.top : undefined
            anchors.topMargin: root.styleIndex === 3 ? 8 : 0
            anchors.horizontalCenter: root.styleIndex === 3 ? undefined : parent.horizontalCenter
            anchors.left: root.styleIndex === 3 ? parent.left : undefined
            stroke: node.muted ? Theme.inkMuted : node.highlighted ? Theme.accent : Theme.ink
            lineWidth: root.styleIndex === 4 ? 1.7 : 1.45
        }
        Rectangle {
            visible: root.styleIndex === 2
            width: 9
            height: 9
            radius: 5
            anchors.horizontalCenter: nodeIcon.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 1
            color: Theme.transparent
            border.width: 1
            border.color: node.muted ? Theme.inkMuted : node.highlighted ? Theme.accent : Theme.ink
        }
        Column {
            anchors.left: root.styleIndex === 3 ? nodeIcon.right : parent.left
            anchors.leftMargin: root.styleIndex === 3 ? 10 : 0
            anchors.right: parent.right
            anchors.top: root.styleIndex === 3 ? parent.top : nodeIcon.bottom
            anchors.topMargin: root.styleIndex === 3 ? 8 : 7
            spacing: 4
            Label {
                text: node.title
                color: node.muted ? Theme.inkMuted : Theme.ink
                font.family: Theme.monoFont
                font.pixelSize: root.spacious ? (root.styleIndex === 4 ? 14 : 12) : 11
                font.bold: node.highlighted && !node.muted
                horizontalAlignment: root.styleIndex === 3 ? Text.AlignLeft : Text.AlignHCenter
                wrapMode: Text.Wrap
                width: parent.width
            }
            Label {
                visible: root.styleIndex === 1 || root.styleIndex === 4
                text: node.detail
                color: Theme.inkMuted
                font.family: Theme.monoFont
                font.pixelSize: root.spacious ? 11 : 10
                horizontalAlignment: root.styleIndex === 3 ? Text.AlignLeft : Text.AlignHCenter
                wrapMode: Text.Wrap
                width: parent.width
            }
        }
        ToolTip.visible: hoverHandler.hovered
        ToolTip.text: node.description
        HoverHandler { id: hoverHandler }
    }

    component FlowArrow: Item {
        property bool correctionStep: false
        width: root.spacious ? (root.styleIndex === 2 ? 34 : 22) : (root.styleIndex === 2 ? 22 : 16)
        height: root.nodeHeight
        Icon {
            anchors.centerIn: parent
            width: root.spacious ? (root.styleIndex === 2 ? 25 : 18) : (root.styleIndex === 2 ? 20 : 16)
            height: width
            kind: "arrow-right"
            visible: root.styleIndex !== 2 || !parent.correctionStep || root.correctionEnabled
            stroke: parent.correctionStep && root.correctionEnabled ? Theme.accent : Theme.inkMuted
            lineWidth: 1.35
        }
    }

    Rectangle {
        x: 0
        y: 0
        width: root.contentWidth
        height: root.contentHeight
        visible: root.styleIndex === 1 || root.styleIndex === 4
        color: root.styleIndex === 1 ? Theme.ledgerSurface : Theme.surface
        border.color: Theme.rule
        border.width: 1
        radius: root.styleIndex === 4 ? 12 : 8
    }

    Canvas {
        id: signalTrack
        visible: root.styleIndex === 2
        width: root.contentWidth
        height: root.contentHeight
        antialiasing: true
        z: 0
        readonly property real speechX: flow.x + speechNode.x + speechNode.width / 2
        readonly property real recognitionX: flow.x + recognitionNode.x + recognitionNode.width / 2
        readonly property real textX: flow.x + textNode.x + textNode.width / 2
        readonly property real trackY: flow.y + root.nodeHeight - 5.5
        readonly property bool correctionEnabled: root.correctionEnabled
        readonly property color trackColor: Theme.rule
        readonly property color routeColor: correctionEnabled ? Theme.accent : Theme.inkMuted
        onSpeechXChanged: requestPaint()
        onRecognitionXChanged: requestPaint()
        onTextXChanged: requestPaint()
        onTrackYChanged: requestPaint()
        onCorrectionEnabledChanged: requestPaint()
        onTrackColorChanged: requestPaint()
        onRouteColorChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onVisibleChanged: if (visible) requestPaint()
        onPaint: {
            const context = getContext("2d")
            context.reset()
            context.lineWidth = 1.25
            context.lineCap = "round"
            context.lineJoin = "round"
            context.strokeStyle = trackColor
            context.beginPath()
            context.moveTo(speechX, trackY)
            context.lineTo(recognitionX, trackY)
            context.stroke()

            context.strokeStyle = routeColor
            context.beginPath()
            context.moveTo(recognitionX, trackY)
            if (correctionEnabled) {
                context.lineTo(textX, trackY)
            } else {
                const bypassY = trackY + 26
                const bend = 8
                context.lineTo(recognitionX, bypassY - bend)
                context.quadraticCurveTo(recognitionX, bypassY, recognitionX + bend, bypassY)
                context.lineTo(textX - bend, bypassY)
                context.quadraticCurveTo(textX, bypassY, textX, bypassY - bend)
                context.lineTo(textX, trackY + 5)
            }
            context.stroke()
            if (!correctionEnabled) {
                const arrowX = (recognitionX + textX) / 2
                const arrowY = trackY + 26
                context.beginPath()
                context.moveTo(arrowX - 5, arrowY - 4)
                context.lineTo(arrowX, arrowY)
                context.lineTo(arrowX - 5, arrowY + 4)
                context.moveTo(textX - 4, trackY + 10)
                context.lineTo(textX, trackY + 5)
                context.lineTo(textX + 4, trackY + 10)
                context.stroke()
            }
        }
    }

    Row {
        id: flow
        x: Math.max(diagramPadding, (root.contentWidth - flow.implicitWidth) / 2)
        y: diagramPadding
        spacing: root.flowSpacing
        z: 1

        FlowNode {
            id: speechNode
            title: "Speech"
            detail: root.styleIndex === 4 ? "raw audio" : "user voice"
            description: "Words spoken by the user"
            iconKind: "mic"
            nodeWidth: root.styleIndex === 3 ? (root.spacious ? 150 : 132) : (root.spacious ? 104 : 66)
        }
        FlowArrow {}
        FlowNode {
            id: recognitionNode
            title: root.styleIndex === 3 ? "Speech recognition" : "Recognition"
            detail: root.styleIndex === 4 ? "audio to text" : "speech model"
            description: "The speech recognition model turns audio into text"
            iconKind: "ear"
            nodeWidth: root.styleIndex === 3 ? (root.spacious ? 190 : 168) : (root.spacious ? 132 : 92)
        }
        FlowArrow { correctionStep: true }
        FlowNode {
            title: root.styleIndex === 3 ? "Language model" : (root.spacious ? "Language model" : "LLM")
            detail: root.styleIndex === 4 ? "correction" : "grammar + terms"
            description: root.correctionEnabled
                ? "LLM means language model. The selected local or cloud model can correct grammar and likely technical terms"
                : "LLM means language model. Correction is off, so recognized text is used directly"
            iconKind: "paper-pencil"
            muted: !root.correctionEnabled
            highlighted: root.correctionEnabled
            nodeWidth: root.styleIndex === 3 ? (root.spacious ? 178 : 150) : (root.spacious ? 124 : 66)
        }
        FlowArrow { correctionStep: true }
        FlowNode {
            id: textNode
            title: root.styleIndex === 3 ? "Final text" : "Text"
            detail: root.styleIndex === 4 ? "review input" : "voice review"
            description: "The final text used by voice review"
            iconKind: "review-ledger"
            nodeWidth: root.styleIndex === 3 ? (root.spacious ? 140 : 118) : (root.spacious ? 104 : 66)
        }
    }

    Label {
        visible: root.styleIndex === 4
        x: diagramPadding + 14
        y: diagramPadding + root.nodeHeight + 3
        width: Math.max(0, root.contentWidth - diagramPadding * 2 - 28)
        text: "Example:  raw \"micro controller\"  →  corrected \"microcontroller\""
        color: Theme.inkMuted
        font.family: Theme.monoFont
        font.pixelSize: root.spacious ? 11 : 10
        wrapMode: Text.Wrap
    }
}
