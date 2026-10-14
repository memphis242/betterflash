import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: root
    property alias currentIndex: picker.currentIndex
    property string localHint: "Runs on this machine and works offline after the model is installed."
    property string remoteHint: "Uses a configured cloud provider and requires a network connection."
    signal activated(int index)
    spacing: 12
    Layout.fillWidth: true
    Layout.minimumWidth: 0
    implicitHeight: picker.implicitHeight
    Label {
        text: "Location of model:"
        color: Theme.inkMuted
        font.pixelSize: 17
        elide: Text.ElideRight
        Layout.alignment: Qt.AlignVCenter
        Layout.minimumWidth: 0
        ToolTip.visible: truncated && hovered
        ToolTip.text: text
    }
    ComboBox {
        id: picker
        model: ["Local (on your machine)", "Remote (cloud-provider)"]
        font.pixelSize: 17
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.preferredWidth: 280
        Layout.maximumWidth: 430
        ToolTip.visible: hovered
        ToolTip.text: currentIndex === 0 ? root.localHint : root.remoteHint
        delegate: ItemDelegate {
            required property int index
            required property string modelData
            width: picker.width
            text: modelData
            font.pixelSize: 17
            highlighted: picker.highlightedIndex === index
            ToolTip.visible: hovered
            ToolTip.text: index === 0 ? root.localHint : root.remoteHint
        }
        onActivated: root.activated(index)
    }
}
