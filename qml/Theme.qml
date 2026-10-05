pragma Singleton
import QtQuick
import QtCore
import QtQml

QtObject {
    id: theme
    readonly property bool hasChoice: preferences.hasThemeChoice
    readonly property bool dark: hasChoice ? preferences.savedDark : Qt.styleHints.colorScheme !== Qt.Light
    readonly property color canvas: dark ? "#211722" : "#f5f0e7"
    readonly property color surface: dark ? "#2c202c" : "#fffaf1"
    readonly property color surfaceRaised: dark ? "#362838" : "#ffffff"
    readonly property color rule: dark ? "#594454" : "#b8a69a"
    readonly property color ink: dark ? "#f7eee8" : "#322832"
    readonly property color inkMuted: dark ? "#c9b7c0" : "#685a63"
    readonly property color accent: dark ? "#e7a17d" : "#a34b34"
    readonly property color onAccent: dark ? "#211722" : "#fffaf1"
    readonly property color accentSoft: dark ? "#563b3f" : "#f1d9cd"
    readonly property color success: dark ? "#a9c79b" : "#38704a"
    readonly property color warning: dark ? "#e6c17a" : "#815714"
    readonly property color error: dark ? "#ee9a9a" : "#a13939"
    readonly property color transparent: "transparent"
    readonly property color scrim: dark ? "#99211722" : "#77322832"
    readonly property string uiFont: "Cantarell"
    readonly property string contentFont: "Caladea"
    readonly property string monoFont: "DejaVu Sans Mono"
    property Settings preferences: Settings {
        category: "appearance"
        property bool savedDark: true
        property bool hasThemeChoice: false
    }
    function toggle() {
        preferences.savedDark = !dark
        preferences.hasThemeChoice = true
    }
}
