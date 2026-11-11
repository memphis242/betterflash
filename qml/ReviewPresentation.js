.pragma library

function cardTags(card) {
    const raw = card && (card.tags || card.labels || []);
    if (Array.isArray(raw))
        return raw.filter(tag => String(tag).trim().length > 0).map(tag => String(tag).trim());
    return String(raw || "").split(",").map(tag => tag.trim()).filter(tag => tag.length > 0);
}

function cardGrade(card) {
    if (!card) return -1;
    const grade = Number(card.sessionGrade);
    return Number.isFinite(grade) ? grade : -1;
}

function gradeColor(grade, palette) {
    switch (grade) {
    case 0: return palette.recallMissed;
    case 1: return palette.recallPartial;
    case 2: return palette.recallHard;
    case 3: return palette.recallGood;
    case 4: return palette.recallEasy;
    default: return palette.rule;
    }
}

function formatElapsed(seconds) {
    const total = Math.max(0, Math.floor(Number(seconds) || 0));
    const minutes = Math.floor(total / 60);
    const remaining = total % 60;
    return (minutes < 10 ? "0" : "") + minutes + ":" + (remaining < 10 ? "0" : "") + remaining;
}

function recallLabel(fraction, points) {
    const count = Math.max(1, Number(points) || 1);
    const recalled = Math.max(0, Math.min(1, Number(fraction)));
    const amount = recalled * count;
    return count > 1 ? Number(amount.toFixed(2)) + " of " + count + " points" : Math.round(recalled * 100) + " of 100 parts";
}
