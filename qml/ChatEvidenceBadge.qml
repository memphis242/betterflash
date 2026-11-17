import QtQuick
import QtQuick.Controls

AppButton {
    id: badge
    required property var evidence
    property bool source: false
    property real maximumWidth: 260
    readonly property string evidenceName: source ? String(evidence.title || "Source")
        : ({web_search: "Web search", search_cards: "Find saved cards", read_card: "Read saved card"})[evidence.name] || "Retrieval"
    width: Math.min(maximumWidth, implicitWidth)
    text: source ? String(evidence.id || "Source") + ": " + evidenceName : evidenceName
    glyph: source ? "book" : "search"
    hint: {
        const lines = [String(evidence.summary || evidence.title || evidenceName)]
        if (source) {
            if (evidence.url) lines.push(String(evidence.url))
            else if (evidence.cardId) lines.push("Saved card: " + evidence.cardId)
        } else {
            const firstLine = String(evidence.details || "").split("\n")[0].slice(0, 220)
            if (firstLine) lines.push(firstLine)
            if (evidence.status === "running") lines.push("Retrieval in progress")
            else if (evidence.resultCount !== undefined) lines.push("Results: " + evidence.resultCount)
        }
        lines.push("Click for details.")
        return lines.join("\n")
    }
    font.pixelSize: 12
    implicitHeight: 32
    Accessible.description: hint
}
