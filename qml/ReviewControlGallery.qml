import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "ReviewControlDesigns.js" as Designs

ApplicationWindow {
    id: gallery
    objectName: "reviewControlGallery"
    width: 1480
    height: 940
    minimumWidth: 900
    minimumHeight: 780
    visible: true
    title: "BetterFlash - Review controls"
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
    readonly property int variantCount: Designs.designs.length
    readonly property var study: Designs.get(designIndex)
    property bool sidebarCollapsed: false
    readonly property bool compact: height < 880

    function selectDesign(index) {
        designIndex = Math.max(0, Math.min(variantCount - 1, index))
        hoverToggle.checked = false
        preview.showHover(false)
        Qt.callLater(preview.resetView)
    }
    function setDarkMode(dark) {
        Theme.preferences.savedDark = dark
        Theme.preferences.hasThemeChoice = true
    }
    function resetPreview() {
        hoverToggle.checked = false
        preview.resetView()
    }
    function actionLabel(action) {
        switch (action) {
        case "defer": return "Defer to queue end"
        case "postpone": return "Later date"
        case "return": return "Return to active card"
        default: return action
        }
    }
    Component.onCompleted: {
        if (!Theme.hasChoice)
            setDarkMode(true)
    }
    Shortcut { sequence: "Alt+Left"; onActivated: gallery.selectDesign(gallery.designIndex - 1) }
    Shortcut { sequence: "Alt+Right"; onActivated: gallery.selectDesign(gallery.designIndex + 1) }
    Shortcut { sequence: "Ctrl+0"; onActivated: gallery.resetPreview() }

    header: Rectangle {
        height: 68
        color: Theme.canvas
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.rule }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 24
            anchors.rightMargin: 24
            spacing: 14
            Icon { kind: "cards"; stroke: Theme.ink; Layout.preferredWidth: 23; Layout.preferredHeight: 23 }
            Label { text: "Review controls"; font.pixelSize: 20; font.bold: true; color: Theme.ink }
            Item { Layout.fillWidth: true }
            Label { text: "5 studies  /  Alt + ← →"; font.pixelSize: 11; color: Theme.inkMuted }
            GlyphButton {
                objectName: "controlThemeToggle"
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
            Layout.preferredWidth: gallery.sidebarCollapsed ? 58 : 236
            Layout.fillHeight: true
            color: Theme.canvas
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.rule }
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 12
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        visible: !gallery.sidebarCollapsed
                        text: "DIRECTIONS"
                        font.pixelSize: 10
                        font.letterSpacing: 1.2
                        color: Theme.inkMuted
                        Layout.fillWidth: true
                    }
                    GlyphButton {
                        objectName: "controlSidebarToggle"
                        glyph: gallery.sidebarCollapsed ? "arrow-right" : "arrow-left"
                        hint: gallery.sidebarCollapsed ? "Expand study list" : "Collapse study list"
                        onClicked: gallery.sidebarCollapsed = !gallery.sidebarCollapsed
                    }
                }
                ListView {
                    id: studyList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 6
                    model: Designs.designs
                    currentIndex: gallery.designIndex
                    boundsBehavior: Flickable.StopAtBounds
                    delegate: Button {
                        required property int index
                        required property var modelData
                        width: studyList.width
                        height: gallery.sidebarCollapsed ? 42 : 52
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
                                font.pixelSize: 12
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
            objectName: "controlMainContent"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: gallery.compact ? 16 : 26
            spacing: gallery.compact ? 12 : 18
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "Defer, schedule, and return to your place."
                    color: Theme.inkMuted
                    font.pixelSize: 12
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                CheckBox {
                    id: hoverToggle
                    objectName: "controlHoverToggle"
                    text: "Preview hover"
                    font.pixelSize: 12
                    ToolTip.visible: hovered
                    ToolTip.text: "Show all three controls in their hover state"
                    onToggled: preview.showHover(checked)
                    indicator: Rectangle {
                        x: hoverToggle.leftPadding
                        y: (hoverToggle.height - height) / 2
                        implicitWidth: 20
                        implicitHeight: 20
                        radius: 3
                        color: Theme.transparent
                        border.color: hoverToggle.checked || hoverToggle.activeFocus ? Theme.accent : Theme.rule
                        border.width: hoverToggle.activeFocus ? 2 : 1
                        Icon {
                            anchors.fill: parent
                            anchors.margins: 3
                            visible: hoverToggle.checked
                            kind: "check"
                            stroke: Theme.accent
                        }
                    }
                }
                GlyphButton {
                    objectName: "controlPrevious"
                    glyph: "arrow-left"
                    hint: "Previous study (Alt+Left)"
                    enabled: gallery.designIndex > 0
                    onClicked: gallery.selectDesign(gallery.designIndex - 1)
                }
                GlyphButton {
                    objectName: "controlNext"
                    glyph: "arrow-right"
                    hint: "Next study (Alt+Right)"
                    enabled: gallery.designIndex < gallery.variantCount - 1
                    onClicked: gallery.selectDesign(gallery.designIndex + 1)
                }
            }

            Rectangle {
                id: captureArea
                objectName: "controlCaptureArea"
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.surface
                radius: 16
                border.color: Theme.rule
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: gallery.compact ? 18 : 24
                    spacing: gallery.compact ? 12 : 18
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 16
                        Label {
                            text: String(gallery.designIndex + 1).padStart(2, "0")
                            color: Theme.inkMuted
                            font.pixelSize: 20
                        }
                        ColumnLayout {
                            spacing: 4
                            Layout.fillWidth: true
                            Label { text: gallery.study.name; color: Theme.ink; font.pixelSize: 22; font.bold: true }
                            Label {
                                text: gallery.study.subtitle
                                color: Theme.inkMuted
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }
                        }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.rule }
                    ReviewControlPreview {
                        id: preview
                        objectName: "controlDesignPreview"
                        designIndex: gallery.designIndex
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 0
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: preview.lastAction.length > 0 ? "Preview action: " + gallery.actionLabel(preview.lastAction)
                        : "Hover for tooltips. Click to try a control."
                    color: Theme.inkMuted
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                AppButton {
                    objectName: "controlReset"
                    text: "Reset preview"
                    hint: "Restore the preview with the active card out of view (Ctrl+0)"
                    onClicked: gallery.resetPreview()
                }
            }
        }
    }
}
