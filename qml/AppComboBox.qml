import QtQuick
import QtQuick.Controls

ComboBox {
    id: control

    implicitHeight: Math.max(38, font.pixelSize + 25)
    implicitWidth: Math.max(180, contentItem.implicitWidth + indicator.width + 18)
    leftPadding: Math.max(8, Math.min(12, height / 3))
    rightPadding: indicator.width + 2
    topPadding: 0
    bottomPadding: 0
    font.family: Theme.uiFont
    hoverEnabled: true

    palette.buttonText: control.enabled ? Theme.ink : Theme.inkMuted
    palette.text: control.enabled ? Theme.ink : Theme.inkMuted
    palette.placeholderText: Theme.inkMuted
    property string comboHint: "Choose an option"
    property bool keyboardCandidateActive: false
    ToolTip.visible: hovered && comboHint.length > 0
    ToolTip.text: comboHint

    function recordNavigation(event) {
        event.accepted = false
        if (!popup.visible)
            return
        switch (event.key) {
        case Qt.Key_Up:
        case Qt.Key_Down:
        case Qt.Key_Home:
        case Qt.Key_End:
        case Qt.Key_PageUp:
        case Qt.Key_PageDown:
            keyboardCandidateActive = true
            break
        default:
            break
        }
    }

    Keys.priority: Keys.BeforeItem
    Keys.onPressed: event => control.recordNavigation(event)

    delegate: ItemDelegate {
        id: option
        required property var model
        required property int index
        readonly property bool selectedEntry: control.currentIndex === index
        readonly property bool keyboardCandidate: control.keyboardCandidateActive
            && control.highlightedIndex === index
        width: ListView.view ? ListView.view.width : control.width
        implicitHeight: Math.max(38, control.font.pixelSize + 25)
        text: control.textRole ? model[control.textRole] : model.modelData
        // Mouse hover is the primary transient highlight. Keyboard navigation
        // gets a thin outline after an explicit navigation key.
        highlighted: option.hovered
        hoverEnabled: true
        leftPadding: 14
        rightPadding: 12
        font.family: control.font.family
        font.pixelSize: control.font.pixelSize
        font.weight: selectedEntry ? Font.Bold : Font.Normal
        Keys.priority: Keys.BeforeItem
        Keys.onPressed: event => control.recordNavigation(event)
        onHoveredChanged: if (option.hovered)
            control.keyboardCandidateActive = false
        contentItem: Label {
            text: option.text
            textFormat: Text.PlainText
            font: option.font
            color: Theme.ink
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 4
            color: option.hovered ? Theme.comboOptionHover : Theme.comboPopupFill
            border.width: option.selectedEntry ? 2 : option.keyboardCandidate ? 1 : 0
            border.color: Theme.accent
            Rectangle {
                visible: option.selectedEntry
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.topMargin: 6
                anchors.bottomMargin: 6
                width: 3
                radius: 1
                color: Theme.accent
            }
        }
    }

    indicator: Item {
        x: control.width - width
        y: 1
        width: Math.max(24, Math.min(40, control.height - 8))
        height: control.height - 2

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 1
            color: control.enabled ? Theme.rule : Theme.inkMuted
        }
        Icon {
            anchors.centerIn: parent
            width: 17
            height: 17
            kind: "chevron"
            lineWidth: 1.8
            stroke: control.enabled ? Theme.ink : Theme.inkMuted
        }
    }

    background: Rectangle {
        radius: 5
        color: !control.enabled ? Theme.comboDisabled
                               : control.popup.visible ? Theme.comboOpen
                               : control.hovered ? Theme.comboHover
                               : Theme.comboFill
        border.width: control.activeFocus || control.contentItem.activeFocus || control.popup.visible ? 2 : 1
        border.color: !control.enabled ? Theme.rule
                                       : control.activeFocus || control.contentItem.activeFocus || control.popup.visible ? Theme.accent
                                       : Theme.rule
    }

    // Keep the editable form as one control. The outer ComboBox owns the
    // border, while this text field supplies editing, selection, and the
    // standard ComboBox editText connection without drawing a second frame.
    contentItem: TextField {
        id: editableText
        implicitWidth: contentWidth
        implicitHeight: contentHeight
        text: control.editable ? control.editText : control.displayText
        enabled: control.editable
        readOnly: control.down
        autoScroll: control.editable
        selectByMouse: control.selectTextByMouse
        inputMethodHints: control.inputMethodHints
        validator: control.validator
        font: control.font
        color: control.enabled ? Theme.ink : Theme.inkMuted
        selectionColor: Theme.accent
        selectedTextColor: Theme.onAccent
        verticalAlignment: Text.AlignVCenter
        leftPadding: 0
        rightPadding: 0
        topPadding: 0
        bottomPadding: 0
        Keys.priority: Keys.BeforeItem
        Keys.onPressed: event => control.recordNavigation(event)
        background: null
    }

    popup.background: Rectangle {
        color: Theme.comboPopupFill
        border.color: Theme.rule
        border.width: 1
        radius: 5
    }

    Connections {
        target: control.popup
        function onOpened() {
            control.keyboardCandidateActive = false
        }
        function onClosed() {
            control.keyboardCandidateActive = false
        }
    }
}
