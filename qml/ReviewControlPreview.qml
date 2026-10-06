import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "QueueDesigns.js" as QueueStudies
import "ReviewControlDesigns.js" as Designs

Item {
    id: preview
    objectName: "controlDesignPreview"
    property int designIndex: 0
    readonly property var study: Designs.get(designIndex)
    property alias queuePreview: queue
    readonly property string queueOrder: QueueStudies.cards.map(card => card.prompt).join("\n")
    readonly property int actionCount: actionCounter
    readonly property string lastAction: actionName
    property bool hoverDemo: false
    property bool voicePreviewEnabled: false
    signal actionInvoked(string action)
    property int actionCounter: 0
    property string actionName: ""
    implicitHeight: 490

    function invokeAction(action) {
        if (action === "return") {
            queue.centerActive()
            return
        }
        actionName = action
        actionCounter += 1
        actionInvoked(action)
    }
    function resetView() {
        actionCounter = 0
        actionName = ""
        hoverDemo = false
        voicePreviewEnabled = false
        queue.resetView()
        Qt.callLater(function() { queue.seekTo(0.4) })
    }
    function showHover(value) {
        hoverDemo = value
    }
    onDesignIndexChanged: Qt.callLater(resetView)
    Component.onCompleted: Qt.callLater(resetView)

    Shortcut {
        sequence: "D"
        context: Qt.WindowShortcut
        enabled: preview.visible
        onActivated: preview.invokeAction("defer")
    }
    Shortcut {
        sequence: "S"
        context: Qt.WindowShortcut
        enabled: preview.visible
        onActivated: preview.invokeAction("postpone")
    }

    Component {
        id: returnControlComponent
        ReviewControlButton {
            objectName: "controlReturn"
            designIndex: preview.designIndex
            action: "return"
            demonstrateHover: preview.hoverDemo
            onClicked: preview.invokeAction("return")
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10
        QueueDesignPreview {
            id: queue
            objectName: "controlQueuePreview"
            designIndex: 3
            returnControl: returnControlComponent
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            Layout.minimumHeight: implicitHeight
            Layout.maximumHeight: implicitHeight
        }
        ScrollView {
            id: reviewBody
            objectName: "controlReviewBody"
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: reviewBody.availableWidth
                spacing: 7
                Label {
                    textFormat: Text.PlainText
                    text: "Question"
                    color: Theme.inkMuted
                    font.family: Theme.monoFont
                    font.pixelSize: 11
                }
                Label {
                    textFormat: Text.PlainText
                    text: "State the Pythagorean theorem."
                    color: Theme.ink
                    font.family: Theme.contentFont
                    font.pixelSize: 20
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: Theme.rule
                }
                Label {
                    textFormat: Text.PlainText
                    text: "Answer"
                    color: Theme.inkMuted
                    font.family: Theme.monoFont
                    font.pixelSize: 11
                }
                Label {
                    textFormat: Text.PlainText
                    text: "In a right triangle, a² + b² = c², where c is the hypotenuse."
                    color: Theme.ink
                    font.family: Theme.contentFont
                    font.pixelSize: 16
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }
        }
        Label {
            textFormat: Text.PlainText
            text: "00:42"
            color: Theme.inkMuted
            font.family: Theme.monoFont
            font.pixelSize: 12
            Layout.alignment: Qt.AlignHCenter
        }
        RowLayout {
            objectName: "controlGradeRow"
            Layout.fillWidth: true
            Layout.preferredHeight: 51
            Layout.minimumHeight: 51
            Layout.maximumHeight: 51
            spacing: 10
            Repeater {
                model: ["Missed", "Partial", "Hard", "Good", "Easy"]
                delegate: AppButton {
                    required property string modelData
                    required property int index
                    text: modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 51
                    padding: 5
                    hint: modelData + " recall (" + (index + 1) + ")"
                    contentItem: Column {
                        spacing: 2
                        Label {
                            width: parent.width
                            textFormat: Text.PlainText
                            text: index + 1
                            horizontalAlignment: Text.AlignHCenter
                            font.family: Theme.monoFont
                            font.pixelSize: 10
                            color: Theme.inkMuted
                        }
                        Label {
                            width: parent.width
                            textFormat: Text.PlainText
                            text: modelData
                            horizontalAlignment: Text.AlignHCenter
                            font.family: Theme.monoFont
                            font.pixelSize: 14
                            color: Theme.ink
                        }
                    }
                    Shortcut {
                        sequence: String(index + 1)
                        context: Qt.WindowShortcut
                        enabled: preview.visible
                        onActivated: preview.invokeAction(modelData.toLowerCase())
                    }
                    onClicked: preview.invokeAction(modelData.toLowerCase())
                }
            }
        }
        Item {
            id: footer
            objectName: "controlFooter"
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            Layout.minimumHeight: 48
            Layout.maximumHeight: 48
            readonly property real pairWidth: preview.study.size * 2 + preview.study.gap
            readonly property real pairLeft: preview.study.placement === "left" || preview.study.placement === "split" ? 0
                : preview.study.placement === "right" ? microphone.x - preview.study.gap - 6 - pairWidth
                : (width - pairWidth) / 2
            Rectangle {
                visible: preview.study.placement === "capsule"
                x: footer.pairLeft - (preview.study.groupPadding || 0)
                y: (parent.height - height) / 2
                width: footer.pairWidth + 2 * (preview.study.groupPadding || 0)
                height: preview.study.size + 2 * (preview.study.groupPadding || 0)
                radius: preview.study.groupRadius || 0
                color: Theme.transparent
                border.color: Theme.rule
                border.width: 1
            }
            ReviewControlButton {
                id: deferButton
                objectName: "controlDefer"
                designIndex: preview.designIndex
                action: "defer"
                x: footer.pairLeft
                y: (parent.height - height) / 2
                demonstrateHover: preview.hoverDemo
                onClicked: preview.invokeAction("defer")
            }
            ReviewControlButton {
                objectName: "controlPostpone"
                designIndex: preview.designIndex
                action: "postpone"
                x: preview.study.placement === "split" ? microphone.x - preview.study.gap - width : footer.pairLeft + deferButton.width + preview.study.gap
                y: (parent.height - height) / 2
                demonstrateHover: preview.hoverDemo
                onClicked: preview.invokeAction("postpone")
            }
            GlyphButton {
                id: microphone
                objectName: "controlMicrophone"
                x: parent.width - width
                y: (parent.height - height) / 2
                glyph: "mic"
                selected: preview.voicePreviewEnabled
                hint: preview.voicePreviewEnabled ? "Disable voice preview" : "Enable voice preview"
                onClicked: preview.voicePreviewEnabled = !preview.voicePreviewEnabled
            }
        }
    }
}
