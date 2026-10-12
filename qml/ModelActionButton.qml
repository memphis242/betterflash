import QtQuick
import QtQuick.Controls

AppButton {
    id: control
    property string actionKind: "download"
    readonly property bool downloadAction: actionKind === "download"
    glyph: downloadAction ? "download" : "check"
    glyphSize: 20
    font.pixelSize: 16
    implicitHeight: 44
    foreground: downloadAction ? Theme.info : Theme.warning
    fill: downloadAction ? Theme.modelDownloadFill : Theme.modelTestFill
    hoverFill: downloadAction ? Theme.modelDownloadHover : Theme.modelTestHover
    outline: downloadAction ? Theme.info : Theme.warning
    focusOutline: outline
    ToolTip.visible: helpHover.hovered && hint.length > 0
    HoverHandler { id: helpHover; enabled: true }
}
