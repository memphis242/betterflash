import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: gallery
    width: 1320
    height: 940
    minimumWidth: 720
    minimumHeight: 540
    visible: true
    title: "BetterFlash - Card timeline studies"
    color: Theme.canvas
    font.family: Theme.uiFont
    font.pixelSize: 14
    palette.windowText: Theme.ink
    palette.text: Theme.ink
    palette.buttonText: Theme.ink
    palette.base: Theme.canvas
    palette.highlight: Theme.accentSoft
    palette.highlightedText: Theme.ink
    Component.onCompleted: {
        if (!Theme.hasChoice) {
            Theme.preferences.savedDark = true
            Theme.preferences.hasThemeChoice = true
        }
    }
    property double referenceTime: Date.now()
    readonly property var sampleEvents: {
        function at(days) { return new Date(referenceTime + days * 86400000).toISOString() }
        return [
            {id: "saved", kind: "saved", at: at(-42)},
            {id: "r1", kind: "review", at: at(-42 + 0.03), grade: 0, recallFraction: 0, responseSeconds: 22.6, variantKey: "forward"},
            {id: "r2", kind: "review", at: at(-41), grade: 1, recallFraction: 0.5, responseSeconds: 18.2, variantKey: "forward"},
            {id: "edit", kind: "modified", at: at(-40.5)},
            {id: "r3", kind: "review", at: at(-39), grade: 2, recallFraction: 1, responseSeconds: 14.1, variantKey: "forward"},
            {id: "r4", kind: "review", at: at(-35), grade: 3, recallFraction: 1, responseSeconds: 9.8, variantKey: "forward"},
            {id: "r5", kind: "review", at: at(-29), grade: 1, recallFraction: 0.75, responseSeconds: 16.3, variantKey: "forward"},
            {id: "r6", kind: "review", at: at(-25), grade: 3, recallFraction: 1, responseSeconds: 10.2, variantKey: "forward"},
            {id: "r7", kind: "review", at: at(-16), grade: 4, recallFraction: 1, responseSeconds: 6.7, variantKey: "forward"},
            {id: "r8", kind: "review", at: at(-5), grade: 4, recallFraction: 1, responseSeconds: 5.4, variantKey: "forward"},
            {id: "now", kind: "now", at: at(0)},
            {id: "due", kind: "due", at: at(3.25)}
        ]
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                Label { text: "Card timeline studies"; color: Theme.ink; font.pixelSize: 22; font.bold: true }
                Label {
                    text: "Three treatments of the same sample history. Scroll each lane to explore earlier events."
                    color: Theme.inkMuted; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true
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
            spacing: 30
            ColumnLayout {
                Layout.fillWidth: true
                Label { text: "Front"; color: Theme.inkMuted; font.pixelSize: 11 }
                Label { text: "Why space reviews apart?"; color: Theme.ink; font.pixelSize: 17; Layout.fillWidth: true; wrapMode: Text.Wrap }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Label { text: "Back"; color: Theme.inkMuted; font.pixelSize: 11 }
                Label { text: "Retrieval after a delay strengthens long-term recall."; color: Theme.ink; font.pixelSize: 15; Layout.fillWidth: true; wrapMode: Text.Wrap }
            }
            ColumnLayout {
                Label { text: "Illustrative data"; color: Theme.inkMuted; font.pixelSize: 11 }
                Label { text: "5 of 8 successful  /  78% recall"; color: Theme.ink; font.pixelSize: 12 }
                Label { text: "42 days recorded  /  next in 3d 6h"; color: Theme.inkMuted; font.pixelSize: 12 }
            }
        }
        ScrollView {
            id: studies
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: studies.availableWidth
                spacing: 14
                Repeater {
                    model: 3
                    delegate: CardTimeline {
                        required property int index
                        Layout.fillWidth: true
                        Layout.preferredHeight: 242
                        variant: index
                        events: gallery.sampleEvents
                    }
                }
            }
        }
    }
}
