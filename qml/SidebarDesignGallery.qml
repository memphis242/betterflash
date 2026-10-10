pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: gallery
    width: 1320
    height: 860
    minimumWidth: 1100
    minimumHeight: 780
    visible: true
    title: "BetterFlash - Sidebar studies"
    color: Theme.canvas
    font.family: Theme.uiFont
    font.pixelSize: 14
    palette.window: Theme.canvas
    palette.windowText: Theme.ink
    palette.text: Theme.ink
    palette.buttonText: Theme.ink
    palette.base: Theme.surface
    palette.highlight: Theme.accentSoft
    palette.highlightedText: Theme.ink
    property int reviewChoice: 0
    property int toggleChoice: 0
    readonly property var reviewChoices: [
        {name: "Turn the card", glyph: "review-flip"},
        {name: "Retrieval stack", glyph: "review-stack"},
        {name: "Review ledger", glyph: "review-ledger"}
    ]
    readonly property var toggleChoices: [
        {name: "Menu / dock", expand: "menu-bars", collapse: "dock-left"},
        {name: "Bold chevron", expand: "chevron-right-bold", collapse: "chevron-left-bold"},
        {name: "Window pane", expand: "panel-open", collapse: "panel-close"},
        {name: "Double chevron", expand: "double-right", collapse: "double-left"},
        {name: "Folded rail", expand: "rail-unfold", collapse: "rail-fold"}
    ]
    Component.onCompleted: {
        if (!Theme.hasChoice) {
            Theme.preferences.savedDark = true
            Theme.preferences.hasThemeChoice = true
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 20
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 7
                Label { text: "Sidebar studies"; color: Theme.ink; font.pixelSize: 24; font.bold: true }
                Label {
                    text: "Choose the glyphs, then try both layouts. Each preview expands independently."
                    color: Theme.inkMuted
                    font.pixelSize: 12
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                }
            }
            GlyphButton {
                glyph: Theme.dark ? "sun" : "moon"
                hint: Theme.dark ? "Switch to light theme" : "Switch to dark theme"
                onClicked: Theme.toggle()
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.rule }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 28
            ColumnLayout {
                Layout.preferredWidth: 310
                Layout.minimumWidth: 310
                Layout.maximumWidth: 310
                Layout.fillHeight: true
                spacing: 10
                Label { text: "Review icon"; color: Theme.ink; font.pixelSize: 16; font.bold: true }
                Repeater {
                    model: gallery.reviewChoices
                    delegate: Button {
                        id: reviewOption
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        Layout.preferredHeight: 55
                        padding: 10
                        Accessible.name: modelData.name
                        ToolTip.visible: hovered
                        ToolTip.text: "Preview the " + modelData.name.toLowerCase() + " review icon"
                        onClicked: gallery.reviewChoice = index
                        contentItem: RowLayout {
                            spacing: 15
                            Icon {
                                kind: reviewOption.modelData.glyph
                                stroke: gallery.reviewChoice === reviewOption.index ? Theme.accent : Theme.ink
                                Layout.preferredWidth: 30
                                Layout.preferredHeight: 30
                                lineWidth: 1.35
                            }
                            Label {
                                text: "0" + (reviewOption.index + 1) + "  " + reviewOption.modelData.name
                                color: Theme.ink
                                font.pixelSize: 13
                                Layout.fillWidth: true
                            }
                        }
                        background: Rectangle {
                            radius: 7
                            color: reviewOption.hovered ? Theme.accentSoft : Theme.transparent
                            border.width: gallery.reviewChoice === reviewOption.index || reviewOption.activeFocus ? 2 : 1
                            border.color: gallery.reviewChoice === reviewOption.index || reviewOption.activeFocus ? Theme.accent : Theme.rule
                        }
                    }
                }
                Label {
                    text: "Expand / collapse"
                    color: Theme.ink
                    font.pixelSize: 16
                    font.bold: true
                    Layout.topMargin: 18
                }
                Repeater {
                    model: gallery.toggleChoices
                    delegate: Button {
                        id: toggleOption
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        Layout.preferredHeight: 52
                        padding: 10
                        Accessible.name: modelData.name
                        ToolTip.visible: hovered
                        ToolTip.text: "Use " + modelData.name.toLowerCase() + " in both previews; left glyph expands, right glyph collapses"
                        onClicked: gallery.toggleChoice = index
                        contentItem: RowLayout {
                            spacing: 12
                            Label {
                                text: "0" + (toggleOption.index + 1) + "  " + toggleOption.modelData.name
                                color: Theme.ink
                                font.pixelSize: 12
                                Layout.fillWidth: true
                            }
                            Icon {
                                kind: toggleOption.modelData.expand
                                stroke: Theme.ink
                                Layout.preferredWidth: 26
                                Layout.preferredHeight: 26
                            }
                            Rectangle { width: 1; height: 22; color: Theme.rule }
                            Icon {
                                kind: toggleOption.modelData.collapse
                                stroke: Theme.ink
                                Layout.preferredWidth: 26
                                Layout.preferredHeight: 26
                            }
                        }
                        background: Rectangle {
                            radius: 7
                            color: toggleOption.hovered ? Theme.accentSoft : Theme.transparent
                            border.width: gallery.toggleChoice === toggleOption.index || toggleOption.activeFocus ? 2 : 1
                            border.color: gallery.toggleChoice === toggleOption.index || toggleOption.activeFocus ? Theme.accent : Theme.rule
                        }
                    }
                }
                Item { Layout.fillHeight: true }
            }
            Repeater {
                model: 2
                delegate: ColumnLayout {
                    id: study
                    required property int index
                    property bool collapsed: true
                    property int selectedPage: 0
                    readonly property var pageNames: ["Review", "Library", "History", "Settings"]
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumWidth: 320
                    spacing: 12
                    Label {
                        text: study.index === 0 ? "01  Focused" : "02  Full height"
                        color: Theme.ink
                        font.pixelSize: 16
                        font.bold: true
                    }
                    Label {
                        text: study.index === 0 ? "56 px buttons, grouped at the top" : "Four equal areas, all space clickable"
                        color: Theme.inkMuted
                        font.pixelSize: 11
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        Layout.minimumHeight: 30
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: Theme.canvas
                        border.color: Theme.rule
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 1
                            spacing: 0
                            Rectangle {
                                Layout.fillWidth: true
                                height: 52
                                color: Theme.surface
                                BrandMark { anchors.left: parent.left; anchors.leftMargin: 18; anchors.verticalCenter: parent.verticalCenter; width: 30; height: 30 }
                                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.rule }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                spacing: 0
                                SidebarNavigation {
                                    Layout.preferredWidth: study.collapsed ? 76 : 184
                                    Layout.fillHeight: true
                                    collapsed: study.collapsed
                                    distributed: study.index === 1
                                    currentIndex: study.selectedPage
                                    reviewGlyph: gallery.reviewChoices[gallery.reviewChoice].glyph
                                    toggleVariant: gallery.toggleChoice
                                    onToggleRequested: study.collapsed = !study.collapsed
                                    onPageRequested: index => study.selectedPage = index
                                }
                                Item {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 18
                                        spacing: 15
                                        Label {
                                            text: study.pageNames[study.selectedPage]
                                            color: Theme.ink
                                            font.pixelSize: 19
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
                                        }
                                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.rule }
                                        Label {
                                            text: "Navigation preview"
                                            color: Theme.inkMuted
                                            font.pixelSize: 11
                                            Layout.fillWidth: true
                                            wrapMode: Text.Wrap
                                        }
                                        Item { Layout.fillHeight: true }
                                    }
                                }
                            }
                        }
                    }
                    Label {
                        text: study.collapsed ? "76 px rail  /  26 px glyphs" : "Expanded  /  26 px glyphs"
                        color: Theme.inkMuted
                        font.pixelSize: 11
                    }
                }
            }
        }
    }
}
