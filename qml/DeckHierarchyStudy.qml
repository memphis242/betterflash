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
    readonly property var captions: ["Parents above, subdecks below", "Follow each branch from left to right"]
    property real viewScale: 1
    readonly property color lineColor: Theme.rule
    readonly property color activeColor: Theme.accent
    property bool selectionNeedsReveal: false
    onSelectedIdChanged: edges.requestPaint()
    onDiagramChanged: edges.requestPaint()
    onLineColorChanged: edges.requestPaint()
    onActiveColorChanged: edges.requestPaint()
    onViewScaleChanged: edges.requestPaint()
    onVisibleChanged: if (visible && selectionNeedsReveal) selectionReveal.restart()

    function generationLabel(depth) {
        switch (depth) {
        case 0: return "Root deck";
        case 1: return "Children";
        case 2: return "Grandchildren";
        default: return "Great-" + "great-".repeat(depth - 3) + "grandchildren";
        }
    }
    function revealSelection() {
        selectionNeedsReveal = true;
        selectionReveal.restart();
    }
    Timer {
        id: selectionReveal
        interval: 16
        onTriggered: {
            if (!graph.visible || viewport.width <= 0 || viewport.height <= 0) return;
            graph.positionSelection();
        }
    }
    function positionSelection() {
        if (!visibleDecks.some(d => d.id === selectedId)) {
            const expanded = Object.assign({}, expandedBranches);
            ancestorIds(selectedId).slice(1).forEach(id => expanded[id] = true);
            expandedBranches = expanded;
        }
        Qt.callLater(function() {
            if (!graph.visible || viewport.width <= 0 || viewport.height <= 0) return;
            const node = graph.diagram.nodes.find(n => n.id === graph.selectedId);
            if (!node) return;
            graph.selectionNeedsReveal = false;
            const x = node.x * graph.viewScale + scene.x;
            const y = node.y * graph.viewScale + scene.y;
            const margin = node.diameter * graph.viewScale / 2 + 24;
            graph.clampScroll(
                x - margin < viewport.contentX || x + margin > viewport.contentX + viewport.width ? x - viewport.width / 2 : viewport.contentX,
                y - margin < viewport.contentY || y + margin > viewport.contentY + viewport.height ? y - viewport.height / 2 : viewport.contentY);
        });
    }

    function clampScroll(x, y) {
        viewport.contentX = Math.max(0, Math.min(Math.max(0, viewport.contentWidth - viewport.width), x));
        viewport.contentY = Math.max(0, Math.min(Math.max(0, viewport.contentHeight - viewport.height), y));
    }
    function nodePosition(id) {
        const node = diagram.nodes.find(n => n.id === id);
        return node ? {x: node.x * viewScale + scene.x - viewport.contentX,
                       y: node.y * viewScale + scene.y - viewport.contentY} : null;
    }
    function restorePosition(id, position) {
        Qt.callLater(function() {
            const node = graph.diagram.nodes.find(n => n.id === id);
            if (node && position)
                graph.clampScroll(node.x * graph.viewScale + scene.x - position.x,
                                  node.y * graph.viewScale + scene.y - position.y);
            else
                graph.clampScroll(viewport.contentX, viewport.contentY);
        });
    }

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
        const position = nodePosition(id);
        const changed = Object.assign({}, expandedBranches);
        changed[id] = !isExpanded(deck);
        expandedBranches = changed;
        deckSelected(id);
        restorePosition(id, position);
    }
    function toggleOverview() {
        const position = nodePosition(selectedId);
        const expanded = ({});
        if (!allExpanded) decks.forEach(d => expanded[d.id] = true);
        expandedBranches = expanded;
        restorePosition(selectedId, position);
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
        viewScale = Math.max(0.5, Math.min(2.25, old * factor));
        Qt.callLater(function() {
            graph.clampScroll(centerX * graph.viewScale + scene.x - viewport.width / 2,
                              centerY * graph.viewScale + scene.y - viewport.height / 2);
        });
    }
    function navigate(id, key) {
        const index = visibleDecks.findIndex(d => d.id === id);
        if (index < 0) return false;
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
                visible: graph.width >= 620
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
                visible: graph.width >= 460
                text: "-"
                implicitWidth: 34
                hint: "Zoom out"
                onClicked: graph.changeZoom(1 / 1.2)
            }
            Label {
                visible: graph.width >= 460
                text: Math.round(graph.viewScale * 100) + "%"
                color: Theme.inkMuted
                font.pixelSize: 11
                Layout.preferredWidth: 44
                horizontalAlignment: Text.AlignHCenter
            }
            AppButton {
                visible: graph.width >= 460
                text: "+"
                implicitWidth: 34
                hint: "Zoom in"
                onClicked: graph.changeZoom(1.2)
            }
            AppButton {
                visible: graph.width >= 460
                text: "100%"
                hint: "Restore the default node size"
                onClicked: graph.changeZoom(1 / graph.viewScale)
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
            onContentXChanged: edges.requestPaint()
            onContentYChanged: edges.requestPaint()
            onWidthChanged: {
                edges.requestPaint();
                if (graph.selectionNeedsReveal) selectionReveal.restart();
            }
            onHeightChanged: {
                edges.requestPaint();
                if (graph.selectionNeedsReveal) selectionReveal.restart();
            }
            ScrollBar.horizontal: ScrollBar { policy: viewport.contentWidth > viewport.width ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }
            ScrollBar.vertical: ScrollBar { policy: viewport.contentHeight > viewport.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }
            WheelHandler {
                target: null
                acceptedModifiers: Qt.ControlModifier
                onWheel: event => {
                    graph.changeZoom(event.angleDelta.y > 0 ? 1.1 : 1 / 1.1)
                    event.accepted = true
                }
            }
            Canvas {
                id: edges
                x: viewport.contentX
                y: viewport.contentY
                width: viewport.width
                height: viewport.height
                onPaint: {
                    const c = getContext("2d");
                    c.reset();
                    // Rasterize only the visible viewport, even for a tall forest.
                    c.translate(scene.x - viewport.contentX, scene.y - viewport.contentY);
                    c.scale(graph.viewScale, graph.viewScale);
                    c.lineCap = "round";
                    if (graph.variant === 1) {
                        c.strokeStyle = graph.lineColor;
                        c.lineWidth = 1;
                        for (const group of graph.diagram.groups) {
                            c.beginPath(); c.moveTo(24, group.top); c.lineTo(graph.diagram.width - 24, group.top); c.stroke();
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
                        } else {
                            const x1 = from.x + from.diameter / 2;
                            const x2 = to.x - to.diameter / 2;
                            const mid = (x1 + x2) / 2;
                            c.moveTo(x1, from.y);
                            c.bezierCurveTo(mid, from.y, mid, to.y, x2, to.y);
                        }
                        c.stroke();
                    }
                }
            }
            Item {
                id: scene
                width: graph.diagram.width
                height: graph.diagram.height
                x: 16
                y: 16
                scale: graph.viewScale
                transformOrigin: Item.TopLeft
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
                            minimumPixelSize: 13
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
            }
            Rectangle {
                visible: graph.variant === 1
                x: viewport.contentX
                y: viewport.contentY
                width: viewport.width
                height: 32
                color: Theme.canvas
                z: 2
                clip: true
                Row {
                    x: scene.x + 75 * graph.viewScale - viewport.contentX
                    y: 9
                    Repeater {
                        model: graph.diagram.depthCount
                        delegate: Label {
                            required property int index
                            width: 300 * graph.viewScale
                            text: graph.generationLabel(index)
                            font.pixelSize: 11
                            color: Theme.inkMuted
                            elide: Text.ElideRight
                            rightPadding: 24
                            ToolTip.visible: generationHover.hovered
                            ToolTip.text: text + " · level " + index
                            HoverHandler { id: generationHover }
                        }
                    }
                }
            }
        }
        Label {
            Layout.fillWidth: true
            Layout.margins: 18
            text: "Scroll to explore. Use + to open branches. Drag to pan. Ctrl+wheel to zoom."
            color: Theme.inkMuted
            font.pixelSize: 11
            horizontalAlignment: Text.AlignHCenter
        }
    }
}
