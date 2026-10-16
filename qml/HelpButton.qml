import QtQuick

GlyphButton {
    id: control
    property string topic: ""
    property string label: ""
    glyph: "help"
    glyphSize: 16
    hint: "Learn about " + (label || "this setting")
    Accessible.name: hint
}
