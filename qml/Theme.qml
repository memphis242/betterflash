pragma Singleton
import QtQuick
import QtCore
import QtQml

QtObject {
    id: theme
    readonly property bool reducedMotion: preferences.reducedMotion
    readonly property bool hasChoice: preferences.hasThemeChoice
    readonly property bool dark: hasChoice ? preferences.savedDark : Qt.styleHints.colorScheme !== Qt.Light
    readonly property color canvas: dark ? "#211722" : "#f5f0e7"
    readonly property color surface: dark ? "#2c202c" : "#fffaf1"
    readonly property color surfaceRaised: dark ? "#362838" : "#ffffff"
    readonly property color rule: dark ? "#594454" : "#b8a69a"
    readonly property color ledgerSurface: dark ? "#312531" : "#fffaf1"
    readonly property color ledgerSurfaceRaised: dark ? "#3a2c39" : "#ffffff"
    readonly property color ledgerRule: dark ? "#655360" : "#ad9b8f"
    readonly property color ink: dark ? "#f7eee8" : "#322832"
    readonly property color inkMuted: dark ? "#c9b7c0" : "#685a63"
    readonly property color accent: dark ? "#e7a17d" : "#a34b34"
    readonly property color onAccent: dark ? "#211722" : "#fffaf1"
    readonly property color accentSoft: dark ? "#563b3f" : "#f1d9cd"
    readonly property color editorTabActive: dark ? "#c6b2e3" : "#c9b2d3"
    readonly property color editorTabActiveInk: dark ? "#211722" : "#322832"
    readonly property color editorTabInactive: dark ? "#3b2e3a" : "#f0e9e4"
    readonly property color editorField: dark ? "#241c28" : "#fffdf8"
    readonly property color comboFill: editorField
    readonly property color comboHover: dark ? "#463144" : "#f7eee1"
    readonly property color comboOpen: dark ? "#563b3f" : "#f1d9cd"
    readonly property color comboDisabled: dark ? "#2a222d" : "#f1ece4"
    readonly property color comboPopupFill: dark ? "#211627" : "#eee3f1"
    readonly property color comboOptionHover: dark ? "#4d375f" : "#d8c4e5"
    readonly property color comboOptionSelected: dark ? "#34253f" : "#e4d4ee"
    readonly property color timelineDue: dark ? "#c6b2e3" : "#74528c"
    readonly property color success: dark ? "#a9c79b" : "#38704a"
    readonly property color warning: dark ? "#e6c17a" : "#815714"
    readonly property color info: dark ? "#a9c7f2" : "#245a8d"
    readonly property color modelDownloadFill: dark ? "#2b3a52" : "#e4edf7"
    readonly property color modelDownloadHover: dark ? "#384d6d" : "#cddfeb"
    readonly property color modelTestFill: dark ? "#4a3d25" : "#f9efce"
    readonly property color modelTestHover: dark ? "#605032" : "#f1dfac"
    readonly property color modelLocationLocalFill: dark ? warning : "#f0d17a"
    readonly property color modelLocationLocalInk: dark ? canvas : ink
    readonly property color modelLocationLocalMutedInk: dark ? "#d6c7a5" : "#765821"
    readonly property color error: dark ? "#ee9a9a" : "#a13939"
    readonly property color recallMissed: dark ? "#ee9a9a" : "#a13939"
    readonly property color recallPartial: dark ? "#e6c17a" : "#815714"
    readonly property color recallHard: dark ? "#e7a17d" : "#a34b34"
    readonly property color recallGood: dark ? "#a9c79b" : "#38704a"
    readonly property color recallEasy: dark ? "#91c9b6" : "#286c5c"
    readonly property color activityLow: dark ? "#3b5140" : "#c3d8bd"
    readonly property color activityMedium: dark ? "#53704e" : "#8cb581"
    readonly property color activityHigh: dark ? "#7d9b69" : "#5e8e56"
    readonly property color activityPeak: dark ? "#b2ca93" : "#315f3c"
    readonly property var recallHoverFills: dark ? ["#54333c", "#54462f", "#593c31", "#344b39", "#304c48"] : ["#f1d6d7", "#f2e6c8", "#f3ded3", "#dcebdc", "#d7ebe5"]
    readonly property var tagFills: dark ? ["#58406d", "#80543f", "#42684f", "#813f4b"] : ["#e4d4ed", "#f0d8c8", "#d4e5d5", "#efd4d7"]
    readonly property var tagInks: dark ? ["#f4e8fb", "#fff0e4", "#e6f5e7", "#ffe8eb"] : ["#4b315c", "#693b26", "#2f5438", "#6b2934"]
    readonly property color transparent: "transparent"
    readonly property color scrim: dark ? "#99211722" : "#77322832"
    readonly property string uiFont: "IBM Plex Mono"
    readonly property string contentFont: "IBM Plex Mono"
    readonly property string monoFont: "IBM Plex Mono"
    property Settings preferences: Settings {
        category: "appearance"
        property bool reducedMotion: false
        property bool savedDark: true
        property bool hasThemeChoice: false
    }
    function toggle() {
        preferences.savedDark = !dark
        preferences.hasThemeChoice = true
    }
    function tagIndex(label) {
        const value = String(label || "")
        let hash = 0
        for (let i = 0; i < value.length; i++)
            hash = (hash * 31 + value.charCodeAt(i)) | 0
        return Math.abs(hash) % tagFills.length
    }
    function tagFill(label) { return tagFills[tagIndex(label)] }
    function tagInk(label) { return tagInks[tagIndex(label)] }
    function recallHoverFill(grade) { return recallHoverFills[Math.max(0, Math.min(4, Number(grade)))] }
}
