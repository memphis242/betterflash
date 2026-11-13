.pragma library

const fields = [
    { key: "createdAt", label: "Creation date" },
    { key: "modifiedAt", label: "Last modified date" }
]

function availableFields() {
    return fields.map(field => ({ key: field.key, label: field.label }))
}
function normalizeKeys(value) {
    if (!Array.isArray(value)) return []
    const result = []
    const seen = new Set()
    for (const item of value) {
        if (!item || typeof item !== "object" || seen.has(item.field)
                || !fields.some(field => field.key === item.field)) continue
        seen.add(item.field)
        result.push({ field: item.field, direction: item.direction === "ascending" ? "ascending" : "descending" })
    }
    return result
}
function readKeys(serialized) {
    if (typeof serialized !== "string" || serialized.length > 8192) return []
    try {
        return normalizeKeys(JSON.parse(serialized))
    } catch (error) {
        console.warn("LIBRARY_SORT_SETTINGS: Saved card sorting could not be read. Choose sort fields again.")
        return []
    }
}
function label(key) {
    const field = fields.find(item => item.key === key)
    return field ? field.label : key
}
function timestamp(value) {
    if (typeof value !== "string" || value.length === 0) return NaN
    return Date.parse(value)
}
function compare(left, right, keys) {
    for (const key of keys) {
        const a = timestamp(left[key.field])
        const b = timestamp(right[key.field])
        const aKnown = Number.isFinite(a)
        const bKnown = Number.isFinite(b)
        if (aKnown !== bKnown) return aKnown ? -1 : 1
        if (!aKnown || a === b) continue
        const order = a < b ? -1 : 1
        return key.direction === "ascending" ? order : -order
    }
    return 0
}
function sortedCards(cards, keys) {
    return cards.map((card, index) => ({ card: card, index: index }))
        .sort((a, b) => compare(a.card, b.card, keys) || a.index - b.index)
        .map(entry => entry.card)
}
function summary(keys) {
    return keys.map((key, index) => (index + 1) + ". " + label(key.field)
        + " (" + (key.direction === "ascending" ? "oldest first" : "newest first") + ")").join("\n")
}
