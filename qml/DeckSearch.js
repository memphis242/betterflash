.pragma library

function normalize(value) {
    return String(value || "").normalize("NFKD").replace(/[\u0300-\u036f]/g, "").toLowerCase().replace(/\s+/g, " ").trim()
}

function distance(left, right, limit) {
    if (Math.abs(left.length - right.length) > limit)
        return limit + 1
    let previous = []
    let beforePrevious = []
    for (let j = 0; j <= right.length; ++j)
        previous.push(j)
    for (let i = 1; i <= left.length; ++i) {
        const current = [i]
        let minimum = i
        for (let j = 1; j <= right.length; ++j) {
            const substitution = left[i - 1] === right[j - 1] ? 0 : 1
            let value = Math.min(current[j - 1] + 1, previous[j] + 1, previous[j - 1] + substitution)
            if (i > 1 && j > 1 && left[i - 1] === right[j - 2] && left[i - 2] === right[j - 1])
                value = Math.min(value, beforePrevious[j - 2] + 1)
            current.push(value)
            minimum = Math.min(minimum, value)
        }
        if (minimum > limit)
            return limit + 1
        beforePrevious = previous
        previous = current
    }
    return previous[right.length]
}

function match(name, query) {
    const value = normalize(name)
    const wanted = normalize(query)
    if (!wanted)
        return { tier: 0, distance: 0, label: "" }
    const words = value.split(/[\s/()]+/)
    const terms = wanted.split(/\s+/)
    if (value === wanted || terms.every(term => words.indexOf(term) >= 0))
        return { tier: 0, distance: 0, label: "Exact" }
    if (value.indexOf(wanted) === 0 || terms.every(term => words.some(word => word.indexOf(term) === 0)))
        return { tier: 1, distance: 0, label: "Prefix" }
    let total = 0
    for (const term of terms) {
        if (term.length < 4)
            return null
        const limit = term.length < 7 ? 1 : 2
        let best = limit + 1
        for (const word of words)
            best = Math.min(best, distance(term, word, limit))
        if (best > limit)
            return null
        total += best
    }
    return { tier: 2, distance: total, label: "Similar spelling" }
}
