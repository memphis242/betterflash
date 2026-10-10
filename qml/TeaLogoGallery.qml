pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: gallery
    objectName: "teaLogoGallery"
    width: 1120
    height: 780
    minimumWidth: 760
    minimumHeight: 650
    visible: true
    title: "BetterFlash - Logo studies"
    color: Theme.canvas
    font.family: Theme.uiFont
    font.pixelSize: 14
    palette.window: Theme.canvas
    palette.windowText: Theme.ink
    palette.text: Theme.ink
    palette.buttonText: Theme.ink
    palette.highlight: Theme.accentSoft
    palette.highlightedText: Theme.ink

    readonly property var logoStudies: [
        { label: "01  Ceramic", caption: "Hand-thrown cup", source: "qrc:/logo-studies/tea-01-ceramic.png" },
        { label: "02  Glass", caption: "Clear glass cup", source: "qrc:/logo-studies/tea-02-glass.png" },
        { label: "03  Porcelain", caption: "Porcelain cup", source: "qrc:/logo-studies/tea-03-porcelain.png" },
        { label: "04  Blueberry", caption: "Blueberry mark", source: "qrc:/logo-studies/blueberry.png" }
    ]

    Component.onCompleted: {
        if (!Theme.hasChoice) {
            Theme.preferences.savedDark = true
            Theme.preferences.hasThemeChoice = true
        }
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
                text: "Logo studies"
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

        Label {
            text: "Tea cup directions beside the original blueberry mark"
            color: Theme.inkMuted
            font.pixelSize: 13
            Layout.fillWidth: true
            Layout.topMargin: 8
            Layout.bottomMargin: 18
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 22
            spacing: 24
            Label {
                text: "In the app - 32 px"
                color: Theme.inkMuted
                font.pixelSize: 11
                font.bold: true
                Layout.fillWidth: true
            }
            Label {
                text: "Large - 112 px"
                color: Theme.inkMuted
                font.pixelSize: 11
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                Layout.preferredWidth: 148
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            Repeater {
                model: gallery.logoStudies

                delegate: ColumnLayout {
                    id: studyRow
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    Layout.minimumHeight: 120
                    Layout.preferredHeight: 120
                    Layout.maximumHeight: 140
                    spacing: 5

                    Label {
                        text: studyRow.modelData.label
                        color: Theme.ink
                        font.pixelSize: 12
                        font.bold: true
                        Layout.fillWidth: true
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 24

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.minimumHeight: 56
                            Layout.preferredHeight: 56
                            Layout.maximumHeight: 56
                            Layout.alignment: Qt.AlignVCenter
                            color: Theme.canvas

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 16
                                anchors.rightMargin: 16
                                spacing: 14

                                Image {
                                    Layout.preferredWidth: 32
                                    Layout.preferredHeight: 32
                                    source: studyRow.modelData.source
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
                                    mipmap: true
                                    asynchronous: false
                                    sourceSize.width: Math.max(1, Math.ceil(width * Math.max(1, Screen.devicePixelRatio)))
                                    sourceSize.height: Math.max(1, Math.ceil(height * Math.max(1, Screen.devicePixelRatio)))
                                    Accessible.name: studyRow.modelData.caption + " app icon"
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 1
                                    Layout.alignment: Qt.AlignBottom
                                    color: Theme.rule
                                    opacity: 0.7
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.preferredWidth: 148
                            Layout.minimumWidth: 148
                            Layout.maximumWidth: 148
                            Layout.preferredHeight: 112
                            Layout.minimumHeight: 112
                            Layout.maximumHeight: 112
                            Layout.alignment: Qt.AlignVCenter
                            Item {
                                Layout.preferredWidth: 148
                                Layout.preferredHeight: 112
                                Layout.minimumHeight: 112
                                Layout.maximumHeight: 112
                                Image {
                                    anchors.centerIn: parent
                                    width: 112
                                    height: 112
                                    source: studyRow.modelData.source
                                    fillMode: Image.PreserveAspectFit
                                    smooth: true
                                    mipmap: true
                                    asynchronous: false
                                    sourceSize.width: Math.max(1, Math.ceil(width * Math.max(1, Screen.devicePixelRatio)))
                                    sourceSize.height: Math.max(1, Math.ceil(height * Math.max(1, Screen.devicePixelRatio)))
                                    Accessible.name: studyRow.modelData.caption + " large icon"
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
