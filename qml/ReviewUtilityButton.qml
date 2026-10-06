import QtQuick

GlyphButton {
    implicitWidth: 44
    implicitHeight: 44
    glyphSize: 22
    accentOnHover: true
    toolTipOnFocus: true
    opacity: enabled ? 1 : 0.55
    Keys.onShortcutOverride: event => {
        if (enabled && activeFocus && event.modifiers === Qt.NoModifier
                && (event.key === Qt.Key_Space || event.key === Qt.Key_Return || event.key === Qt.Key_Enter))
            event.accepted = true
    }
    Keys.onReturnPressed: {
        if (enabled)
            clicked()
    }
    Keys.onEnterPressed: {
        if (enabled)
            clicked()
    }
}
