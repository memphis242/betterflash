pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "DeckStudyData.js" as Study

Item {
    id: graph
    required property var decks
    property int variant: 0
    property string selectedId: "biology"
    property bool detailsVisible: true
    signal deckSelected(string deckId)
    signal showDetailsRequested()
    property var expandedBranches: ({})
    readonly property var visibleDecks: collectVisible()
    readonly property var diagram: Study.layout(visibleDecks, variant)
    readonly property bool allExpanded: decks.filter(d => decks.some(c => c.parent === d.id)).every(d => isExpanded(d))
    readonly property var ancestors: ancestorIds(selectedId)
    readonly property var captions: ["Parents above, subdecks below", "Follow each branch from left to right", "Independent deck trees, arranged radially"]
    property bool autoFit: true
    property real manualScale: 1
    readonly property real fitScale: Math.max(0.3, Math.min((viewport.width - 32) / diagram.width, (viewport.height - 32) / diagram.height))
    readonly property real viewScale: autoFit ? fitScale : manualScale
    readonly property color lineColor: Theme.rule
    readonly property color activeColor: Theme.accent
    onSelectedIdChanged: edges.requestPaint()
    onDiagramChanged: edges.requestPaint()
    onLineColorChanged: edges.requestPaint()
    onActiveColorChanged: edges.requestPaint()

    function isExpanded(deck) {
        return expandedBranches[deck.id] === undefined ? !deck.parent : expandedBranches[deck.id];
    }
    function collectVisible() {
        const visible = [];
        const visited = [];
        function visit(deck) {
            if (visited.indexOf(deck.id) >= 0) return;
            visited.push(deck.id); visible.push(deck);
            if (graph.isExpanded(deck))
                graph.decks.filter(d => d.parent === deck.id).forEach(visit);
        }
        decks.filter(d => !d.parent).forEach(visit);
        return visible;
    }
    function toggleBranch(id) {
        const deck = decks.find(d => d.id === id);
        if (!deck) return;
        const changed = Object.assign({}, expandedBranches);
        changed[id] = !isExpanded(deck);
        expandedBranches = changed;
        deckSelected(id);
    }
    function toggleOverview() {
        const expanded = ({});
        if (!allExpanded) decks.forEach(d => expanded[d.id] = true);
        expandedBranches = expanded;
        fit();
    }

    function ancestorIds(id) {
        const result = [];
        let node = decks.find(d => d.id === id);
        while (node && result.indexOf(node.id) < 0) {
            result.push(node.id);
            node = decks.find(d => d.id === node.parent);
        }
        return result;
    }
    function changeZoom(factor) {
        const old = viewScale;
        const centerX = (viewport.contentX + viewport.width / 2 - scene.x) / old;
        const centerY = (viewport.contentY + viewport.height / 2 - scene.y) / old;
        manualScale = Math.max(fitScale * 0.75, Math.min(2.25, old * factor));
        autoFit = false;
        Qt.callLater(function() {
            viewport.contentX = Math.max(0, Math.min(viewport.contentWidth - viewport.width, centerX * graph.viewScale + scene.x - viewport.width / 2));
            viewport.contentY = Math.max(0, Math.min(viewport.contentHeight - viewport.height, centerY * graph.viewScale + scene.y - viewport.height / 2));
        });
    }
    function fit() {
        autoFit = true;
        viewport.contentX = 0;
        viewport.contentY = 0;
    }
    function navigate(id, key) {
        const index = visibleDecks.findIndex(d => d.id === id);
        let target = null;
        switch (key) {
        case Qt.Key_Left: target = visibleDecks[Math.max(0, index - 1)]; break;
        case Qt.Key_Right: target = visibleDecks[Math.min(visibleDecks.length - 1, index + 1)]; break;
        case Qt.Key_Up: target = decks.find(d => d.id === visibleDecks[index].parent); break;
        case Qt.Key_Down:
            target = decks.find(d => d.parent === id);
            if (target && !isExpanded(visibleDecks[index])) toggleBranch(id);
            break;
        case Qt.Key_Plus:
        case Qt.Key_Minus: toggleBranch(id); return true;
        default: return false;
        }
        if (target) {
            deckSelected(target.id);
            Qt.callLater(function() {
                for (let i = 0; i < nodeRepeater.count; ++i) {
                    const item = nodeRepeater.itemAt(i);
                    if (item && item.modelData.id === target.id) {
                        item.forceActiveFocus();
                        const x = item.modelData.x * graph.viewScale + scene.x;
                        const y = item.modelData.y * graph.viewScale + scene.y;
                        if (x < viewport.contentX + 70 || x > viewport.contentX + viewport.width - 70)
                            viewport.contentX = Math.max(0, Math.min(viewport.contentWidth - viewport.width, x - viewport.width / 2));
                        if (y < viewport.contentY + 70 || y > viewport.contentY + viewport.height - 70)
                            viewport.contentY = Math.max(0, Math.min(viewport.contentHeight - viewport.height, y - viewport.height / 2));
                    }
                }
            });
        }
        return true;
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 18
            Layout.preferredHeight: 58
            spacing: 8
            Label {
                text: graph.captions[graph.variant]
                color: Theme.inkMuted
                font.pixelSize: 11
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            AppButton {
                text: graph.allExpanded ? "Overview" : "Expand all"
                hint: graph.allExpanded ? "Show roots and their immediate subdecks" : "Show every level of every deck tree"
                onClicked: graph.toggleOverview()
            }
            AppButton {
                text: "-"
                implicitWidth: 34
                hint: "Zoom out"
                onClicked: graph.changeZoom(1 / 1.2)
            }
            Label {
                text: Math.round(graph.viewScale * 100) + "%"
                color: Theme.inkMuted
                font.pixelSize: 11
                Layout.preferredWidth: 44
                horizontalAlignment: Text.AlignHCenter
            }
            AppButton {
                text: "+"
                implicitWidth: 34
                hint: "Zoom in"
                onClicked: graph.changeZoom(1.2)
            }
            AppButton {
                text: "Fit"
                hint: "Fit every deck in the graph area"
                onClicked: graph.fit()
            }
            PaneToggleButton {
                visible: !graph.detailsVisible
                collapsed: true
                rightSide: true
                hint: "Open details for the selected deck"
                onClicked: graph.showDetailsRequested()
            }
        }
        Flickable {
            id: viewport
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: Math.max(width, graph.diagram.width * graph.viewScale + 32)
            contentHeight: Math.max(height, graph.diagram.height * graph.viewScale + 32)
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            WheelHandler {
                target: null
                acceptedModifiers: Qt.ControlModifier
                onWheel: event => {
                    graph.changeZoom(event.angleDelta.y > 0 ? 1.1 : 1 / 1.1)
                    event.accepted = true
                }
            }
            Item {
                id: scene
                width: graph.diagram.width
                height: graph.diagram.height
                x: (viewport.contentWidth - width * graph.viewScale) / 2
                y: (viewport.contentHeight - height * graph.viewScale) / 2
                scale: graph.viewScale
                transformOrigin: Item.TopLeft
                Canvas {
                    id: edges
                    anchors.fill: parent
                    onPaint: {
                        const c = getContext("2d");
                        c.reset();
                        c.lineCap = "round";
                        if (graph.variant === 1) {
                            c.strokeStyle = graph.lineColor;
                            c.lineWidth = 1;
                            for (const root of graph.diagram.nodes.filter(n => n.root)) {
                                const y = 35 + root.firstSlot * 106;
                                c.beginPath(); c.moveTo(35, y); c.lineTo(width - 35, y); c.stroke();
                            }
                        }
                        for (const edge of graph.diagram.edges) {
                            const from = edge.from, to = edge.to;
                            const selected = graph.ancestors.indexOf(to.id) >= 0;
                            c.strokeStyle = selected ? graph.activeColor : graph.lineColor;
                            c.lineWidth = selected ? 2.5 : 1.5;
                            c.beginPath();
                            if (graph.variant === 0) {
                                const y1 = from.y + from.diameter / 2;
                                const y2 = to.y - to.diameter / 2;
                                c.moveTo(from.x, y1);
                                c.bezierCurveTo(from.x, (y1 + y2) / 2, to.x, (y1 + y2) / 2, to.x, y2);
                            } else if (graph.variant === 1) {
                                const x1 = from.x + from.diameter / 2;
                                const x2 = to.x - to.diameter / 2;
                                const mid = (x1 + x2) / 2;
                                c.moveTo(x1, from.y);
                                c.bezierCurveTo(mid, from.y, mid, to.y, x2, to.y);
                            } else {
                                const dx = to.x - from.x, dy = to.y - from.y;
                                const distance = Math.sqrt(dx * dx + dy * dy);
                                const ux = dx / distance, uy = dy / distance;
                                const x1 = from.x + ux * from.diameter / 2;
                                const y1 = from.y + uy * from.diameter / 2;
                                const x2 = to.x - ux * to.diameter / 2;
                                const y2 = to.y - uy * to.diameter / 2;
                                c.moveTo(x1, y1);
                                c.quadraticCurveTo((x1 + x2) / 2 - uy * 14, (y1 + y2) / 2 + ux * 14, x2, y2);
                            }
                            c.stroke();
                        }
                    }
                }
                Repeater {
                    id: nodeRepeater
                    model: graph.diagram.nodes
                    delegate: Button {
                        id: node
                        required property var modelData
                        readonly property bool chosen: graph.selectedId === modelData.id
                        readonly property bool onPath: graph.ancestors.indexOf(modelData.id) >= 0
                        readonly property int childCount: graph.decks.filter(d => d.parent === modelData.id).length
                        x: modelData.x - width / 2
                        y: modelData.y - height / 2
                        width: modelData.diameter
                        height: width
                        padding: 9
                        Accessible.name: modelData.deck.name
                        Accessible.description: "Deck in " + modelData.deck.parentPath
                        ToolTip.visible: hovered || activeFocus
                        ToolTip.text: modelData.deck.name + " / " + modelData.deck.total + " cards including subdecks"
                        onClicked: graph.deckSelected(modelData.id)
                        TapHandler {
                            gesturePolicy: TapHandler.WithinBounds
                            onDoubleTapped: if (node.childCount > 0) graph.toggleBranch(node.modelData.id)
                        }
                        Keys.onPressed: event => {
                            if (graph.navigate(modelData.id, event.key)) event.accepted = true
                        }
                        contentItem: Label {
                            text: node.modelData.deck.label
                            textFormat: Text.PlainText
                            color: node.chosen ? Theme.accent : Theme.ink
                            font.pixelSize: node.modelData.root ? 20 : 18
                            font.bold: node.modelData.root || node.chosen
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            wrapMode: Text.WordWrap
                            fontSizeMode: Text.Fit
                            minimumPixelSize: 10
                        }
                        background: Rectangle {
                            radius: width / 2
                            color: node.hovered ? Theme.accentSoft : node.chosen ? Theme.surfaceRaised : Theme.canvas
                            border.width: node.chosen ? 3 : node.activeFocus ? 2.5 : node.modelData.root ? 2 : 1.5
                            border.color: node.chosen || node.activeFocus || node.onPath ? Theme.accent : Theme.ledgerRule
                            Rectangle {
                                visible: node.chosen
                                anchors.fill: parent
                                anchors.margins: 7
                                radius: width / 2
                                color: Theme.transparent
                                border.color: Theme.accent
                                border.width: 1
                            }
                        }
                        GlyphButton {
                            visible: node.childCount > 0
                            x: parent.width - 4
                            y: parent.height * 0.63
                            width: 26
                            height: 26
                            glyphSize: 12
                            glyph: graph.isExpanded(node.modelData.deck) ? "minus" : "plus"
                            hint: (graph.isExpanded(node.modelData.deck) ? "Hide" : "Show") + " " + node.childCount + " subdecks in " + node.modelData.deck.name
                            onClicked: graph.toggleBranch(node.modelData.id)
                        }
                    }
                }
                Row {
                    visible: graph.variant === 1
                    x: 75
                    y: 14
                    spacing: 0
                    Repeater {
                        model: Math.max(1, ...graph.diagram.nodes.map(n => n.depth)) + 1
                        delegate: Label {
                            required property int index
                            width: 300
                            text: index === 0 ? "DECK" : "SUBDECK"
                            font.pixelSize: 11
                            color: Theme.inkMuted
                        }
                    }
                }
            }
        }
        Label {
            Layout.fillWidth: true
            Layout.margins: 18
            text: "Select a deck. Use + to open branches. Drag to pan. Ctrl+wheel to zoom."
            color: Theme.inkMuted
            font.pixelSize: 11
            horizontalAlignment: Text.AlignHCenter
        }
    }
}
