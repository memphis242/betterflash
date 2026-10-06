import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "QueueDesigns.js" as Studies

Item {
    id: preview
    objectName: "queueDesignPreview"
    property int designIndex: 0
    readonly property var design: Studies.get(designIndex)
    readonly property var tokens: Theme.dark ? design.dark : design.light
    property int selectedIndex: 2
    readonly property int count: Studies.cards.length
    property alias contentX: timeline.contentX
    readonly property real minimumScroll: 0
    readonly property real maximumScroll: Math.max(0, (count - 1) * timeline.stride)
    readonly property real minimumContentX: minimumScroll
    readonly property real maximumContentX: maximumScroll
    readonly property bool activeOffscreen: selectedIndex >= 0 && (timeline.leading + selectedIndex * timeline.stride + design.cardWidth <= timeline.contentX || timeline.leading + selectedIndex * timeline.stride >= timeline.contentX + timeline.width)
    implicitHeight: design.cardHeight + 84

    function selectItem(index) {
        if (index >= 0 && index < count)
            selectedIndex = index
    }
    function centerActive() {
        timeline.cancelFlick()
        timeline.contentX = Math.max(minimumScroll, Math.min(maximumScroll, selectedIndex * timeline.stride))
    }
    function returnToCurrent() { centerActive() }
    function seekTo(fraction) {
        timeline.cancelFlick()
        timeline.contentX = minimumScroll + Math.max(0, Math.min(1, fraction)) * (maximumScroll - minimumScroll)
    }
    function resetView() {
        selectedIndex = 2
        Qt.callLater(centerActive)
    }
    onDesignIndexChanged: resetView()
    Component.onCompleted: Qt.callLater(centerActive)

    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            Layout.minimumHeight: 30
            Layout.maximumHeight: 30
            Item { Layout.fillWidth: true }
            AppButton {
                objectName: "designReturn"
                visible: preview.activeOffscreen
                text: "Return to active card"
                hint: "Center the active card without changing your review queue"
                implicitHeight: 30
                padding: 6
                font.pixelSize: 11
                onClicked: preview.centerActive()
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: preview.design.cardHeight + 20
            color: preview.design.frame === "inset" ? preview.tokens.panel : preview.tokens.transparent
            radius: preview.design.style === "folio" ? 14 : 5
            border.color: preview.tokens.faint
            border.width: preview.design.frame === "inset" ? 1 : 0
            Rectangle {
                visible: preview.design.frame === "rules"
                anchors.top: parent.top
                width: parent.width
                height: 1
                color: preview.tokens.rule
            }
            Rectangle {
                visible: preview.design.frame === "rules"
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: preview.tokens.rule
            }
            Flickable {
                id: timeline
                objectName: "designTimeline"
                anchors.fill: parent
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                boundsMovement: Flickable.StopAtBounds
                flickableDirection: Flickable.HorizontalFlick
                activeFocusOnTab: true
                readonly property real stride: preview.design.cardWidth + preview.design.gap
                readonly property real leading: Math.max(0, (width - preview.design.cardWidth) / 2)
                readonly property int firstVisible: Math.max(0, Math.min(preview.count, Math.floor((contentX - leading) / stride) - 1))
                readonly property int lastVisible: Math.max(firstVisible, Math.min(preview.count, Math.ceil((contentX + width - leading) / stride) + 1))
                property int focusedIndex: preview.selectedIndex
                contentWidth: width + preview.maximumScroll
                contentHeight: height
                Accessible.name: "Review queue design preview"
                Keys.onLeftPressed: moveFocus(Math.max(0, focusedIndex - 1))
                Keys.onRightPressed: moveFocus(Math.min(preview.count - 1, focusedIndex + 1))
                Keys.onReturnPressed: preview.selectItem(focusedIndex)
                Keys.onEnterPressed: preview.selectItem(focusedIndex)
                Keys.onSpacePressed: preview.selectItem(focusedIndex)
                function moveFocus(index) {
                    focusedIndex = index
                    const left = leading + index * stride
                    if (left < contentX)
                        contentX = Math.max(0, left)
                    else if (left + preview.design.cardWidth > contentX + width)
                        contentX = Math.min(preview.maximumScroll, left + preview.design.cardWidth - width)
                }
                Row {
                    x: timeline.leading + timeline.firstVisible * timeline.stride
                    y: 10
                    spacing: preview.design.gap
                    Repeater {
                        model: timeline.lastVisible - timeline.firstVisible
                        delegate: Item {
                            id: slot
                            required property int index
                            readonly property int cardIndex: timeline.firstVisible + index
                            readonly property var card: Studies.cards[cardIndex] || ({ subject: "", prompt: "" })
                            readonly property string style: preview.design.style
                            readonly property bool selected: preview.selectedIndex === cardIndex
                            readonly property bool reviewed: cardIndex < 2
                            readonly property string outcome: cardIndex === 0 ? "Good" : "Partial"
                            readonly property color stateColor: cardIndex === 0 ? preview.tokens.success : preview.tokens.partial
                            readonly property real viewportLeft: timeline.leading + cardIndex * timeline.stride - timeline.contentX
                            readonly property real disclosure: reviewed || selected ? 1 : Math.max(0, Math.min(1, (timeline.width - viewportLeft) / width))
                            readonly property string ordinal: (cardIndex + 1 < 10 ? "0" : "") + (cardIndex + 1)
                            readonly property real renderedWidth: width
                            readonly property real renderedLeft: timeline.leading + cardIndex * timeline.stride
                            objectName: "designCard" + cardIndex
                            width: preview.design.cardWidth
                            height: preview.design.cardHeight
                            opacity: selected ? 1 : reviewed ? 0.78 : 0.42 + 0.58 * disclosure
                            Accessible.role: Accessible.Button
                            Accessible.name: card.prompt + (selected ? ", active card" : reviewed ? ", reviewed " + outcome : ", pending card")
                            Accessible.onPressAction: preview.selectItem(cardIndex)
                            HoverHandler { id: cardHover }
                            ToolTip.visible: cardHover.hovered
                            ToolTip.text: card.prompt + (reviewed ? " - reviewed " + outcome : "")
                            TapHandler {
                                onTapped: {
                                    timeline.focusedIndex = slot.cardIndex
                                    timeline.forceActiveFocus()
                                    preview.selectItem(slot.cardIndex)
                                }
                            }
                            Rectangle {
                                id: cardSurface
                                anchors.fill: parent
                                color: slot.style === "numbered" || slot.style === "margin" ? preview.tokens.transparent : slot.selected ? preview.tokens.raised : preview.tokens.surface
                                radius: preview.design.radius
                                border.width: slot.style === "numbered" || slot.style === "margin" || slot.style === "instrument" ? 0 : slot.selected ? 2 : 1
                                border.color: slot.selected ? preview.tokens.accent : slot.reviewed ? slot.stateColor : preview.tokens.rule
                            }
                            Rectangle {
                                visible: slot.style === "film"
                                anchors.fill: parent
                                anchors.margins: 5
                                radius: 1
                                color: preview.tokens.transparent
                                border.color: preview.tokens.rule
                                border.width: 1
                            }
                            Rectangle {
                                visible: slot.style === "margin" || slot.style === "numbered"
                                x: slot.style === "margin" ? 36 : 56
                                y: 8
                                width: 1
                                height: parent.height - 16
                                color: slot.selected ? preview.tokens.accent : preview.tokens.rule
                            }
                            Rectangle {
                                visible: slot.style === "margin" || slot.style === "numbered"
                                anchors.bottom: parent.bottom
                                width: parent.width
                                height: slot.selected ? 2 : 1
                                color: slot.selected ? preview.tokens.accent : slot.reviewed ? slot.stateColor : preview.tokens.rule
                            }
                            Rectangle {
                                visible: slot.style === "tabs" || slot.style === "academic"
                                x: 1
                                y: 1
                                width: parent.width - 2
                                height: slot.style === "tabs" ? 35 : 34
                                radius: preview.design.radius
                                color: preview.tokens.panel
                            }
                            Rectangle {
                                visible: slot.style === "archive" || slot.style === "paper" || slot.style === "academic" || slot.style === "instrument"
                                x: 12
                                y: slot.style === "paper" ? 37 : 35
                                width: parent.width - 24
                                height: 1
                                color: preview.tokens.rule
                            }
                            Rectangle {
                                visible: slot.style === "folio"
                                x: 14
                                y: parent.height - 34
                                width: parent.width - 28
                                height: 1
                                color: preview.tokens.faint
                            }
                            Rectangle {
                                visible: slot.style === "bookmark"
                                x: parent.width / 2 - 14
                                y: -5
                                width: 28
                                height: 17
                                radius: 2
                                color: preview.tokens.surface
                                border.width: slot.selected ? 2 : 1
                                border.color: slot.selected ? preview.tokens.accent : preview.tokens.rule
                            }
                            Repeater {
                                model: slot.style === "instrument" ? 4 : 0
                                delegate: Item {
                                    required property int index
                                    x: index % 2 === 0 ? 0 : slot.width - 10
                                    y: index < 2 ? 0 : slot.height - 10
                                    width: 10
                                    height: 10
                                    Rectangle {
                                        width: parent.width
                                        height: slot.selected ? 2 : 1
                                        y: index < 2 ? 0 : parent.height - height
                                        color: slot.selected ? preview.tokens.accent : preview.tokens.rule
                                    }
                                    Rectangle {
                                        width: slot.selected ? 2 : 1
                                        height: parent.height
                                        x: index % 2 === 0 ? 0 : parent.width - width
                                        color: slot.selected ? preview.tokens.accent : preview.tokens.rule
                                    }
                                }
                            }
                            Label {
                                id: ordinalLabel
                                x: slot.style === "numbered" ? 5 : slot.style === "margin" ? 4 : slot.style === "ledger" ? 12 : 14
                                y: slot.style === "numbered" ? 34 : slot.style === "margin" ? 43 : slot.style === "ledger" ? 35 : 12
                                visible: slot.style === "numbered" || slot.style === "margin" || slot.style === "ledger"
                                text: slot.ordinal
                                color: slot.selected ? preview.tokens.accent : preview.tokens.muted
                                font.family: preview.design.labelFont
                                font.pixelSize: slot.style === "numbered" ? 30 : slot.style === "margin" ? 18 : 15
                            }
                            Label {
                                id: topicLabel
                                x: slot.style === "numbered" ? 70 : slot.style === "margin" ? 49 : slot.style === "ledger" ? 50 : 14
                                y: slot.style === "bookmark" ? 27 : 12
                                width: parent.width - x - 14
                                text: slot.style === "film" ? slot.ordinal + " / " + preview.count : slot.style === "instrument" ? "CARD " + slot.ordinal + "  /  " + slot.card.subject : slot.card.subject.toUpperCase()
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                                color: preview.tokens.muted
                                font.family: preview.design.labelFont
                                font.pixelSize: 11
                                font.letterSpacing: slot.style === "folio" || slot.style === "tabs" ? 0.8 : 0
                            }
                            Label {
                                id: promptLabel
                                x: slot.style === "numbered" ? 70 : slot.style === "margin" ? 49 : slot.style === "ledger" ? 50 : slot.style === "assessment" ? 55 : 14
                                y: slot.style === "ledger" ? 33 : slot.style === "numbered" ? 34 : slot.style === "bookmark" ? 55 : 45
                                width: parent.width - x - (slot.style === "ledger" ? 39 : 14)
                                height: parent.height - y - (slot.style === "folio" ? 45 : slot.style === "bookmark" ? 39 : slot.style === "ledger" ? 12 : 31)
                                text: slot.card.prompt
                                textFormat: Text.PlainText
                                color: preview.tokens.ink
                                font.family: preview.design.bodyFont
                                font.pixelSize: preview.design.bodySize
                                wrapMode: Text.Wrap
                                elide: Text.ElideRight
                                maximumLineCount: slot.style === "bookmark" ? 6 : slot.style === "ledger" ? 3 : slot.style === "numbered" ? 4 : 4
                                verticalAlignment: slot.style === "folio" ? Text.AlignVCenter : Text.AlignTop
                            }
                            Rectangle {
                                visible: slot.style === "assessment"
                                x: 14
                                y: 47
                                width: 26
                                height: 26
                                radius: 13
                                color: preview.tokens.transparent
                                border.width: slot.selected ? 2 : 1
                                border.color: slot.selected ? preview.tokens.accent : slot.reviewed ? slot.stateColor : preview.tokens.rule
                                Label {
                                    anchors.centerIn: parent
                                    text: slot.reviewed ? slot.outcome.charAt(0) : slot.ordinal
                                    color: slot.reviewed ? slot.stateColor : preview.tokens.muted
                                    font.family: preview.design.labelFont
                                    font.pixelSize: 11
                                }
                            }
                            Label {
                                x: slot.style === "margin" ? 49 : slot.style === "numbered" ? 70 : 14
                                y: parent.height - 23
                                width: parent.width - x - 14
                                visible: slot.style !== "ledger" && slot.style !== "film"
                                text: slot.reviewed ? slot.outcome : slot.style === "instrument" ? "QUESTION  " + slot.ordinal + " / " + preview.count : slot.ordinal + " of " + preview.count
                                color: slot.reviewed ? slot.stateColor : preview.tokens.muted
                                font.family: preview.design.labelFont
                                font.pixelSize: 11
                            }
                            Rectangle {
                                visible: slot.style === "ledger"
                                x: parent.width - 28
                                y: 33
                                width: 18
                                height: 24
                                radius: 3
                                color: preview.tokens.transparent
                                border.width: slot.selected ? 2 : 1
                                border.color: slot.selected ? preview.tokens.accent : slot.reviewed ? slot.stateColor : preview.tokens.rule
                                Label {
                                    anchors.centerIn: parent
                                    text: slot.reviewed ? slot.outcome.charAt(0) : "·"
                                    color: slot.reviewed ? slot.stateColor : preview.tokens.muted
                                    font.family: preview.design.labelFont
                                    font.pixelSize: 11
                                }
                            }
                            Rectangle {
                                visible: timeline.activeFocus && timeline.focusedIndex === slot.cardIndex
                                anchors.fill: parent
                                anchors.margins: -3
                                radius: preview.design.radius + 3
                                color: preview.tokens.transparent
                                border.color: preview.tokens.muted
                                border.width: 1
                            }
                        }
                    }
                }
            }
            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 24
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: preview.design.frame === "inset" ? preview.tokens.panel : preview.tokens.canvas }
                    GradientStop { position: 1; color: preview.tokens.transparent }
                }
            }
            Rectangle {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 24
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: preview.tokens.transparent }
                    GradientStop { position: 1; color: preview.design.frame === "inset" ? preview.tokens.panel : preview.tokens.canvas }
                }
            }
        }
        ScrollBar {
            id: positionBar
            objectName: "designScrollbar"
            readonly property real extent: timeline.width + preview.maximumScroll - preview.minimumScroll
            Layout.fillWidth: true
            Layout.preferredHeight: 18
            orientation: Qt.Horizontal
            policy: ScrollBar.AlwaysOn
            active: true
            interactive: preview.maximumScroll > preview.minimumScroll
            activeFocusOnTab: interactive
            hoverEnabled: true
            topPadding: 6
            bottomPadding: 6
            size: extent > 0 ? timeline.width / extent : 1
            position: extent > 0 ? (timeline.contentX - preview.minimumScroll) / extent : 0
            Accessible.role: Accessible.ScrollBar
            Accessible.name: "Queue position"
            ToolTip.visible: hovered
            ToolTip.text: "Scroll the queue; Home and End reach the first and last card"
            function applyPosition() {
                timeline.contentX = Math.max(preview.minimumScroll, Math.min(preview.maximumScroll, preview.minimumScroll + position * extent))
            }
            onPositionChanged: if (pressed) applyPosition()
            onPressedChanged: if (pressed) applyPosition()
            Keys.onLeftPressed: preview.seekTo((timeline.contentX - timeline.stride) / preview.maximumScroll)
            Keys.onRightPressed: preview.seekTo((timeline.contentX + timeline.stride) / preview.maximumScroll)
            Keys.onPressed: event => {
                switch (event.key) {
                case Qt.Key_Home: preview.seekTo(0); break
                case Qt.Key_End: preview.seekTo(1); break
                case Qt.Key_PageUp: preview.seekTo((timeline.contentX - timeline.width) / preview.maximumScroll); break
                case Qt.Key_PageDown: preview.seekTo((timeline.contentX + timeline.width) / preview.maximumScroll); break
                default: return
                }
                event.accepted = true
            }
            background: Rectangle {
                anchors.centerIn: parent
                width: parent.width
                height: 2
                radius: 1
                color: preview.tokens.rule
            }
            contentItem: Rectangle {
                implicitHeight: 6
                radius: 3
                color: positionBar.pressed || positionBar.activeFocus ? preview.tokens.accent : preview.tokens.muted
            }
        }
    }
}
