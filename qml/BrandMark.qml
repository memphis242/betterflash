import QtQuick
import QtQuick.Controls
import QtQuick.Window

Item {
    id: mark

    property string variant: "walnut"
    readonly property string assetSource: {
        switch (variant) {
        case "walnut-blueberry":
            return "qrc:/branding/walnut-blueberry.png"
        case "walnut-spinach":
            return "qrc:/branding/walnut-spinach.png"
        case "walnut":
        default:
            return "qrc:/branding/walnut.png"
        }
    }
    property string accessibleLabel: "BetterFlash walnut mark"
    implicitWidth: 32
    implicitHeight: 32
    Accessible.name: accessibleLabel
    Accessible.description: "BetterFlash application mark"

    Image {
        id: image
        anchors.fill: parent
        source: mark.assetSource
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        asynchronous: false
        sourceSize.width: Math.max(1, Math.ceil(width * Math.max(1, Screen.devicePixelRatio)))
        sourceSize.height: Math.max(1, Math.ceil(height * Math.max(1, Screen.devicePixelRatio)))
    }

    ToolTip.visible: hoverHandler.hovered
    ToolTip.text: mark.accessibleLabel
    HoverHandler { id: hoverHandler }
}
