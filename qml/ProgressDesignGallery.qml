pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: gallery
    objectName: "progressDesignGallery"
    width: 1140
    height: 620
    minimumWidth: 720
    minimumHeight: 480
    visible: true
    title: "BetterFlash - Progress studies"
    color: Theme.canvas
    font.family: Theme.uiFont
    font.pixelSize: 14
    palette.window: Theme.canvas
    palette.windowText: Theme.ink
    palette.text: Theme.ink
    palette.buttonText: Theme.ink
    palette.highlight: Theme.accentSoft
    palette.highlightedText: Theme.ink

    readonly property int cardCount: 12
    property int reviewedCount: 7
    readonly property var grades: [4, 3, 2, 1, 0, 3, 4]

    Component.onCompleted: {
        if (!Theme.hasChoice) {
            Theme.preferences.savedDark = true
            Theme.preferences.hasThemeChoice = true
        }
    }

    function setReviewed(value) {
        reviewedCount = Math.max(0, Math.min(cardCount, Math.round(value)))
    }

    Shortcut {
        sequence: "Left"
        onActivated: gallery.setReviewed(gallery.reviewedCount - 1)
    }
    Shortcut {
        sequence: "Right"
        onActivated: gallery.setReviewed(gallery.reviewedCount + 1)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 36
        anchors.rightMargin: 36
        anchors.topMargin: 28
        anchors.bottomMargin: 26
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 42
            Label {
                text: "Progress studies"
                color: Theme.ink
                font.pixelSize: 22
                font.bold: true
                Layout.fillWidth: true
            }
            GlyphButton {
                objectName: "galleryThemeToggle"
                glyph: Theme.dark ? "sun" : "moon"
                hint: Theme.dark ? "Switch to light theme" : "Switch to dark theme"
                onClicked: Theme.toggle()
            }
        }

        Item { Layout.preferredHeight: 30 }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 30
            Repeater {
                model: ["01  Cut ribbon", "02  Inset ledger", "03  Chevron chain"]
                delegate: RowLayout {
                    id: specimen
                    required property string modelData
                    required property int index
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 26
                    Label {
                        text: specimen.modelData
                        color: Theme.inkMuted
                        font.pixelSize: 12
                        font.bold: true
                        Layout.preferredWidth: 168
                        Layout.alignment: Qt.AlignVCenter
                    }
                    ProgressDesignBar {
                        variant: specimen.index
                        cardCount: gallery.cardCount
                        reviewedCount: gallery.reviewedCount
                        grades: gallery.grades
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.rule
            opacity: 0.7
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            spacing: 16
            Label {
                text: "Reviewed"
                color: Theme.inkMuted
                font.pixelSize: 12
            }
            Slider {
                id: progressSlider
                objectName: "reviewedSlider"
                from: 0
                to: gallery.cardCount
                stepSize: 1
                value: gallery.reviewedCount
                Layout.fillWidth: true
                Accessible.name: "Reviewed cards"
                onMoved: gallery.setReviewed(value)
                onValueChanged: if (pressed) gallery.setReviewed(value)
            }
            Label {
                text: gallery.reviewedCount + " of " + gallery.cardCount
                color: Theme.ink
                font.bold: true
                font.pixelSize: 12
                Layout.preferredWidth: 72
                horizontalAlignment: Text.AlignRight
            }
        }
    }
}
