import QtQuick

Item {
    id: mark

    implicitWidth: 28
    implicitHeight: 28

    Image {
        anchors.fill: parent
        fillMode: Image.PreserveAspectFit
        source: Theme.dark ? "qrc:/branding/recall-fold.svg" : "qrc:/branding/recall-fold-light.svg"
        sourceSize.width: Math.max(1, Math.ceil(mark.width * Screen.devicePixelRatio))
        sourceSize.height: Math.max(1, Math.ceil(mark.height * Screen.devicePixelRatio))
        smooth: true
        mipmap: true
        asynchronous: false
    }
}
