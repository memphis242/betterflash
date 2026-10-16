.pragma library

const descriptions = {
    commandPalette: "Open the searchable command, deck, and card launcher.",
    deckSearch: "Find a deck by name in the Library.",
    newCard: "Create a new flashcard.",
    newDeck: "Create a new deck.",
    editCard: "Edit the selected flashcard.",
    review: "Reveal the answer during review.",
    previousCard: "Move to the previous review card.",
    nextCard: "Move to the next review card.",
    gradeMissed: "Mark the answer as missed.",
    gradePartial: "Mark the answer as partially recalled.",
    gradeHard: "Mark the answer as hard to recall.",
    gradeGood: "Mark the answer as correctly recalled.",
    gradeEasy: "Mark the answer as easily recalled.",
    defer: "Move the current card to the end of the review queue.",
    postpone: "Change the current card's due date.",
    pause: "Pause or resume the current review.",
    microphone: "Start or cancel one voice utterance; in Always on mode, start or stop listening.",
    reviewPage: "Open the Review page.",
    libraryPage: "Open the Library page.",
    historyPage: "Open the History page.",
    settingsPage: "Open the Settings page.",
    editorSplit: "Insert the front and back separator or move between those card fields.",
    editorCloze: "Hide selected words within a sentence as a cloze.",
    editorImage: "Insert an image into the card.",
    saveCard: "Save the card being edited."
}

function forAction(action, fallback) {
    return descriptions[action] || String(fallback || "Run this command.") + "."
}
