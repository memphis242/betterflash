import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: gallery
    width: 1320
    height: 700
    minimumWidth: 720
    minimumHeight: 540
    visible: true
    title: "BetterFlash - Calendar trace preview"
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
            {id: "saved", kind: "saved", at: at(-410)},
            {id: "r1", kind: "review", at: at(-400), grade: 0, recallFraction: 0, responseSeconds: 22.6, variantKey: "forward"},
            {id: "r2", kind: "review", at: at(-330), grade: 1, recallFraction: 0.5, responseSeconds: 18.2, variantKey: "forward"},
            {id: "edit", kind: "modified", at: at(-290)},
            {id: "r3", kind: "review", at: at(-220), grade: 2, recallFraction: 1, responseSeconds: 14.1, variantKey: "forward"},
            {id: "r4", kind: "review", at: at(-60), grade: 3, recallFraction: 1, responseSeconds: 9.8, variantKey: "forward"},
            {id: "r5", kind: "review", at: at(-18), grade: 1, recallFraction: 0.75, responseSeconds: 16.3, variantKey: "forward"},
            {id: "r6", kind: "review", at: at(-10), grade: 3, recallFraction: 1, responseSeconds: 10.2, variantKey: "forward"},
            {id: "r7", kind: "review", at: at(-2), grade: 4, recallFraction: 1, responseSeconds: 6.7, variantKey: "forward"},
            {id: "r8", kind: "review", at: at(-0.8), grade: 4, recallFraction: 1, responseSeconds: 5.4, variantKey: "forward"},
            {id: "now", kind: "now", at: at(0)},
            {id: "due", kind: "due", at: at(1.25)}
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
                Label { text: "Calendar trace preview"; color: Theme.ink; font.pixelSize: 22; font.bold: true }
                Label {
                    text: "Illustrative history. Zoom from a day to a year, or drag the calendar to explore earlier reviews."
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
                Row {
                    spacing: 14
                    Label { text: "78% recall"; color: Theme.recallHard; font.pixelSize: 14; font.bold: true }
                    Label { text: "Best time 5.4 s"; color: Theme.ink; font.pixelSize: 14 }
                }
                Label { text: "5 of 8 successful / 410 days recorded"; color: Theme.inkMuted; font.pixelSize: 11 }
            }
        }
        CalendarTrace {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 340
            events: gallery.sampleEvents
            historyStart: gallery.sampleEvents[0].at
            historyKey: "illustrative-history"
        }
        Label {
            Layout.fillWidth: true
            text: "Wheel or drag to pan. Ctrl+wheel or +/- to zoom. Today returns to the present. Home / End move to the bounds."
            color: Theme.inkMuted
            font.pixelSize: 12
            wrapMode: Text.Wrap
        }
    }
}
