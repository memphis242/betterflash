import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "QueueDesigns.js" as Studies

ApplicationWindow {
    id: gallery
    objectName: "queueDesignGallery"
    width: 1480
    height: 920
    minimumWidth: 900
    minimumHeight: 680
    visible: true
    title: "BetterFlash - Queue studies"
    color: Theme.canvas
    font.family: Theme.uiFont
    font.pixelSize: 14
    palette.window: Theme.canvas
    palette.windowText: Theme.ink
    palette.text: Theme.ink
    palette.buttonText: Theme.ink
    palette.highlight: Theme.accentSoft
    palette.highlightedText: Theme.ink
    property int designIndex: 0
    readonly property int variantCount: Studies.designs.length
    readonly property var study: Studies.designs[designIndex]
    readonly property alias queuePreview: queuePreview
    property bool captureMode: false
    property bool sidebarCollapsed: false
    readonly property bool compact: height < 800

    Component.onCompleted: {
        if (!Theme.hasChoice)
            setDarkMode(true)
    }

    function selectDesign(index) {
        designIndex = Math.max(0, Math.min(variantCount - 1, index))
        Qt.callLater(queuePreview.resetView)
    }
    function setDarkMode(dark) {
        Theme.preferences.savedDark = dark
        Theme.preferences.hasThemeChoice = true
    }
    Shortcut { sequence: "Alt+Left"; onActivated: gallery.selectDesign(gallery.designIndex - 1) }
    Shortcut { sequence: "Alt+Right"; onActivated: gallery.selectDesign(gallery.designIndex + 1) }
    Shortcut { sequence: "Ctrl+0"; onActivated: queuePreview.resetView() }

    header: Rectangle {
        height: 78
        color: Theme.canvas
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.rule }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 24
            anchors.rightMargin: 24
            spacing: 14
            Icon { kind: "cards"; stroke: Theme.ink; Layout.preferredWidth: 23; Layout.preferredHeight: 23 }
            ColumnLayout {
                spacing: 3
                Label { text: "Queue studies"; font.pixelSize: 20; font.bold: true; color: Theme.ink }
                Label { text: "12 directions for focused review"; font.pixelSize: 11; color: Theme.inkMuted }
            }
            Item { Layout.fillWidth: true }
            Label { text: "Alt + ← / →  switch study"; font.pixelSize: 11; color: Theme.inkMuted }
            GlyphButton {
                objectName: "galleryThemeToggle"
                glyph: Theme.dark ? "sun" : "moon"
                hint: Theme.dark ? "Switch to light theme" : "Switch to dark theme"
                onClicked: Theme.toggle()
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            id: sidebar
            objectName: "gallerySidebar"
            Layout.preferredWidth: gallery.sidebarCollapsed ? 58 : 258
            Layout.fillHeight: true
            color: Theme.canvas
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.rule }
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        visible: !gallery.sidebarCollapsed
                        text: "DIRECTIONS"
                        color: Theme.inkMuted
                        font.pixelSize: 10
                        font.letterSpacing: 1.5
                        Layout.fillWidth: true
                    }
                    GlyphButton {
                        objectName: "gallerySidebarToggle"
                        glyph: gallery.sidebarCollapsed ? "arrow-right" : "arrow-left"
                        hint: gallery.sidebarCollapsed ? "Expand study list" : "Collapse study list"
                        onClicked: gallery.sidebarCollapsed = !gallery.sidebarCollapsed
                    }
                }
                ListView {
                    id: studyList
                    objectName: "galleryStudyList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 5
                    model: Studies.designs
                    currentIndex: gallery.designIndex
                    boundsBehavior: Flickable.StopAtBounds
                    delegate: Button {
                        required property var modelData
                        required property int index
                        width: studyList.width
                        height: gallery.sidebarCollapsed ? 42 : 49
                        padding: gallery.sidebarCollapsed ? 0 : 12
                        Accessible.name: (index + 1) + ". " + modelData.name
                        ToolTip.visible: hovered
                        ToolTip.text: modelData.subtitle
                        onClicked: gallery.selectDesign(index)
                        contentItem: RowLayout {
                            spacing: 12
                            Label {
                                text: String(index + 1).padStart(2, "0")
                                color: gallery.designIndex === index ? Theme.accent : Theme.inkMuted
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignHCenter
                                Layout.fillWidth: gallery.sidebarCollapsed
                            }
                            Label {
                                visible: !gallery.sidebarCollapsed
                                text: modelData.name
                                color: Theme.ink
                                font.pixelSize: 13
                                font.bold: gallery.designIndex === index
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                        background: Rectangle {
                            radius: 6
                            color: gallery.designIndex === index ? Theme.surfaceRaised : Theme.transparent
                            border.width: gallery.designIndex === index ? 1 : 0
                            border.color: Theme.accent
                        }
                    }
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                }
            }
        }

        ColumnLayout {
            objectName: "galleryMainContent"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: gallery.compact ? 16 : 28
            spacing: gallery.compact ? 12 : 22
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "Compare card shape, type, spacing, and emphasis."
                    color: Theme.inkMuted
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                GlyphButton {
                    objectName: "galleryPrevious"
                    glyph: "arrow-left"
                    hint: "Previous study (Alt+Left)"
                    enabled: gallery.designIndex > 0
                    onClicked: gallery.selectDesign(gallery.designIndex - 1)
                }
                GlyphButton {
                    objectName: "galleryNext"
                    glyph: "arrow-right"
                    hint: "Next study (Alt+Right)"
                    enabled: gallery.designIndex < gallery.variantCount - 1
                    onClicked: gallery.selectDesign(gallery.designIndex + 1)
                }
            }

            Rectangle {
                id: captureArea
                objectName: "designCaptureArea"
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(410, queuePreview.implicitHeight + 180)
                color: Theme.surface
                radius: 16
                border.width: 1
                border.color: Theme.rule
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 24
                    spacing: 18
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 16
                        Label {
                            text: String(gallery.designIndex + 1).padStart(2, "0")
                            font.pixelSize: 22
                            color: Theme.inkMuted
                        }
                        ColumnLayout {
                            spacing: 4
                            Layout.fillWidth: true
                            Label {
                                text: gallery.study.name
                                color: Theme.ink
                                font.family: "Cantarell"
                                font.pixelSize: 25
                                font.weight: Font.DemiBold
                            }
                            Label {
                                text: gallery.study.subtitle
                                color: Theme.inkMuted
                                font.family: "Cantarell"
                                font.pixelSize: 14
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }
                        }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.rule }
                    QueueDesignPreview {
                        id: queuePreview
                        objectName: "queueDesignPreview"
                        designIndex: gallery.designIndex
                        Layout.fillWidth: true
                        Layout.preferredHeight: implicitHeight
                    }
                    Item { Layout.fillHeight: true }
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: "Foundations / 24 cards"
                            color: Theme.inkMuted
                            font.pixelSize: 11
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            text: "2 reviewed · 22 remaining"
                            color: Theme.inkMuted
                            font.pixelSize: 11
                        }
                    }
                }
            }

            RowLayout {
                visible: !gallery.compact
                Layout.fillWidth: true
                spacing: 28
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Label { text: "WHAT TO COMPARE"; font.pixelSize: 10; font.letterSpacing: 1.2; color: Theme.inkMuted }
                    Label {
                        text: "Readability at a glance, the active card’s emphasis, and how clearly the queue conveys what is behind and ahead."
                        color: Theme.ink
                        font.family: "Cantarell"
                        font.pixelSize: 17
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }
                Rectangle { width: 1; Layout.preferredHeight: 72; color: Theme.rule }
                ColumnLayout {
                    Layout.preferredWidth: 236
                    spacing: 8
                    Label { text: "TRY THE INTERACTION"; font.pixelSize: 10; font.letterSpacing: 1.2; color: Theme.inkMuted }
                    Label {
                        text: "Scroll the queue. Select a card. Return to the active card when it leaves view."
                        color: Theme.ink
                        font.family: "Cantarell"
                        font.pixelSize: 15
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: gallery.study.cardWidth + " × " + gallery.study.cardHeight + " px cards   /   " + gallery.study.gap + " px spacing"
                    color: Theme.inkMuted
                    font.pixelSize: 11
                    Layout.fillWidth: true
                }
                AppButton {
                    objectName: "galleryReset"
                    text: "Reset preview"
                    hint: "Restore the shared sample queue and active card (Ctrl+0)"
                    onClicked: queuePreview.resetView()
                }
            }
        }
    }
}
