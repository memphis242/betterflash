import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: gallery
    objectName: "transcriptFlowGallery"
    width: 1180
    height: 1040
    minimumWidth: 820
    minimumHeight: 650
    visible: true
    title: "BetterFlash - Transcript flow studies"
    color: Theme.canvas
    font.family: Theme.uiFont
    font.pixelSize: 14
    palette.window: Theme.canvas
    palette.windowText: Theme.ink
    palette.text: Theme.ink
    palette.buttonText: Theme.ink
    palette.highlight: Theme.accentSoft
    palette.highlightedText: Theme.ink
    property bool correctionEnabled: true

    Component.onCompleted: {
        if (!Theme.hasChoice)
            Theme.preferences.savedDark = true
    }

    header: Rectangle {
        height: 86
        color: Theme.canvas
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.rule }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 28
            anchors.rightMargin: 28
            spacing: 14
            BrandMark { Layout.preferredWidth: 34; Layout.preferredHeight: 34 }
            ColumnLayout {
                spacing: 3
                Label { text: "Transcript correction flows"; color: Theme.ink; font.pixelSize: 20; font.bold: true }
                Label { text: "Five ways to explain speech → recognition → language model → final text"; color: Theme.inkMuted; font.pixelSize: 11 }
            }
            Item { Layout.fillWidth: true }
            Label { text: "Illustrative UI studies"; color: Theme.inkMuted; font.pixelSize: 11 }
            AppSwitch {
                text: "Correction"
                checked: gallery.correctionEnabled
                onToggled: gallery.correctionEnabled = checked
                ToolTip.visible: hovered
                ToolTip.text: "Route through the highlighted language model, or bypass it when correction is off"
            }
            GlyphButton {
                objectName: "galleryThemeToggle"
                glyph: Theme.dark ? "sun" : "moon"
                hint: Theme.dark ? "Switch to light theme" : "Switch to dark theme"
                onClicked: Theme.toggle()
            }
        }
    }

    Flickable {
        id: scroll
        anchors.fill: parent
        anchors.margins: 24
        anchors.topMargin: 20
        clip: true
        contentWidth: width
        contentHeight: studies.implicitHeight + 24
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        ColumnLayout {
            id: studies
            width: scroll.width
            spacing: 18
            Repeater {
                model: [
                    { name: "Glyph chain", subtitle: "Unboxed icons, compact labels, and quiet arrows", style: 0 },
                    { name: "Ledger lane", subtitle: "One shared frame with rules and explanatory stage notes", style: 1 },
                    { name: "Signal path", subtitle: "A sustained track with markers instead of node boxes", style: 2 },
                    { name: "Editorial stages", subtitle: "Icons beside plain-language stage descriptions", style: 3 },
                    { name: "Transcript strip", subtitle: "A framed pipeline with a clearly labeled illustrative example", style: 4 }
                ]
                delegate: ColumnLayout {
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    spacing: 7
                    Label {
                        text: String(index + 1).padStart(2, "0") + "  " + modelData.name
                        color: Theme.ink
                        font.family: Theme.monoFont
                        font.pixelSize: 17
                        font.bold: true
                    }
                    Label {
                        text: modelData.subtitle
                        color: Theme.inkMuted
                        font.pixelSize: 13
                    }
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: flowPreview.implicitHeight + 20
                        TranscriptCorrectionFlow {
                            id: flowPreview
                            anchors.fill: parent
                            anchors.margins: 10
                            correctionEnabled: gallery.correctionEnabled
                            styleIndex: modelData.style
                            spacious: true
                            interactive: false
                        }
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                Layout.topMargin: 4
                text: "The examples use illustrative labels only. They do not access models, recordings, cards, or saved credentials."
                color: Theme.inkMuted
                font.pixelSize: 11
                wrapMode: Text.Wrap
            }
        }
    }
}
