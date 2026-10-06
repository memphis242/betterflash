.pragma library

const designs = [
    {
        name: "Utility circles",
        subtitle: "Outlined circles keep the review tools together at the lower left.",
        placement: "left", size: 36, radius: 18, buttonStyle: "circle", iconSize: 18,
        gap: 10, returnWidth: 30, returnHeight: 30,
        glyphs: { defer: "queue-tail", postpone: "calendar-day", return: "return-card" }
    },
    {
        name: "Quiet tools",
        subtitle: "Unframed tools sit at the center; a short underline marks hover and focus.",
        placement: "center", size: 34, radius: 4, buttonStyle: "plain", iconSize: 18,
        gap: 16, returnWidth: 32, returnHeight: 28,
        glyphs: { defer: "queue-lines-tail", postpone: "calendar-forward", return: "return-card-arrow" }
    },
    {
        name: "Shared capsule",
        subtitle: "A single capsule holds the pair, with a distinct surface for the tool under the pointer.",
        placement: "capsule", size: 40, radius: 8, buttonStyle: "capsule", iconSize: 18,
        gap: 2, groupPadding: 4, groupRadius: 24, returnWidth: 38, returnHeight: 30,
        glyphs: { defer: "queue-stack-tail", postpone: "calendar-page", return: "return-bookmark" }
    },
    {
        name: "Ledger split",
        subtitle: "Compact square tools occupy opposite ends of the ledger, leaving its center open.",
        placement: "split", size: 32, radius: 2, buttonStyle: "square", iconSize: 16,
        gap: 10, returnWidth: 32, returnHeight: 28,
        glyphs: { defer: "queue-tail", postpone: "calendar-forward", return: "return-card" }
    },
    {
        name: "Right dock",
        subtitle: "Larger raised tools share a dock beside the microphone at the lower right.",
        placement: "right", size: 44, radius: 8, buttonStyle: "raised", iconSize: 20,
        gap: 8, returnWidth: 42, returnHeight: 30,
        glyphs: { defer: "queue-lines-tail", postpone: "calendar-day", return: "return-card-arrow" }
    }
]
function get(index) { return designs[Math.max(0, Math.min(designs.length - 1, index))] }
