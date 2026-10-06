.pragma library

const dark = {
    canvas: "#211722", panel: "#281d28", surface: "#312531", raised: "#3a2c39",
    ink: "#f7eee8", muted: "#c9b7c0", rule: "#655360", faint: "#493846",
    accent: "#e7a17d", success: "#a9c79b", partial: "#e6c17a", transparent: "transparent"
}
const light = {
    canvas: "#f5f0e7", panel: "#eee6db", surface: "#fffaf1", raised: "#ffffff",
    ink: "#322832", muted: "#685a63", rule: "#ad9b8f", faint: "#ddd0c3",
    accent: "#a34b34", success: "#38704a", partial: "#815714", transparent: "transparent"
}
const darkPaper = {
    canvas: dark.canvas, panel: "#30242d", surface: "#e6d9c6", raised: "#f1e6d4",
    ink: "#392d30", muted: "#625047", rule: "#9b8877", faint: "#cbb8a0",
    accent: "#9b492f", success: "#3b663f", partial: "#785a18", transparent: "transparent"
}
const lightPaper = {
    canvas: light.canvas, panel: "#e8ddcb", surface: "#fff8e9", raised: "#fffcef",
    ink: "#392d30", muted: "#625047", rule: "#9b8877", faint: "#dac9b0",
    accent: "#9b492f", success: "#3b663f", partial: "#785a18", transparent: "transparent"
}
function variant(name, subtitle, style, width, height, gap, font, bodySize, radius, frame) {
    return { name: name, subtitle: subtitle, style: style, width: width, cardWidth: width,
        cardHeight: height, gap: gap, bodyFont: font, bodySize: bodySize,
        labelFont: "IBM Plex Mono", font: font, radius: radius, frame: frame, dark: dark, light: light }
}
const designs = [
    variant("Archive index", "A quiet catalog of knowledge, with ruled headers and readable serif prompts.", "archive", 194, 126, 14, "Caladea", 18, 3, "inset"),
    variant("Reading margin", "Tall reading cards with a numbered margin and a clear text column.", "margin", 204, 162, 20, "Caladea", 19, 2, "open"),
    variant("Study folios", "Generous study surfaces, a calm course label, and a separate recall footer.", "folio", 232, 148, 16, "Cantarell", 18, 13, "none"),
    variant("Review ledger", "Wide, compact entries with an exact position and a restrained outcome column.", "ledger", 286, 94, 10, "IBM Plex Mono", 13, 2, "rules"),
    variant("Technical plate", "Square instrument plates with precise labels and corner marks for selection.", "instrument", 218, 140, 12, "Adwaita Mono", 14, 0, "inset"),
    variant("Course tabs", "Course labels become the tabs of a compact study notebook.", "tabs", 186, 154, 14, "Cantarell", 17, 6, "rules"),
    variant("Contact sheet", "A dense sequence of double-framed prompts for scanning a long review queue.", "film", 166, 118, 8, "Cascadia Code NF", 13, 2, "inset"),
    variant("Numbered notes", "Open typography, generous numbered margins, and a single supporting rule.", "numbered", 244, 152, 22, "Caladea", 19, 0, "open"),
    variant("Paper slips", "Warm paper inside the dark workspace, with a handwritten-notebook rhythm.", "paper", 198, 156, 16, "Caladea", 19, 3, "inset"),
    variant("Academic register", "An academic header and a spacious question field share one precise frame.", "academic", 248, 142, 14, "Cantarell", 17, 3, "rules"),
    variant("Study bookmarks", "Tall, narrow markers put more of the queue in view without shrinking type.", "bookmark", 144, 194, 14, "Cantarell", 16, 5, "none"),
    variant("Recall record", "A question and its recall mark sit side by side, like a compact assessment sheet.", "assessment", 272, 120, 12, "IBM Plex Mono", 14, 3, "open")
]
function selectionPalette(base, accent) { return Object.assign({}, base, { accent: accent }) }
designs[1].dark = selectionPalette(dark, "#c6afd9")
designs[1].light = selectionPalette(light, "#72568b")
designs[2].dark = selectionPalette(dark, "#83c6ba")
designs[2].light = selectionPalette(light, "#286d63")
designs[4].dark = selectionPalette(dark, "#f3a45e")
designs[4].light = selectionPalette(light, "#994b19")
designs[9].dark = selectionPalette(dark, "#e6a6b8")
designs[9].light = selectionPalette(light, "#9a455e")
designs[8].dark = darkPaper
designs[8].light = lightPaper

const cards = [
    { subject: "C++", prompt: "What does std::expected<T, E> represent?" },
    { subject: "C++", prompt: "What binds a resource to an object’s lifetime?" },
    { subject: "Geometry", prompt: "State the Pythagorean theorem." },
    { subject: "Calculus", prompt: "What is the derivative of sin x?" },
    { subject: "Databases", prompt: "Which guarantee makes committed changes survive a restart?" },
    { subject: "C++", prompt: "How do a vector’s size and capacity differ?" },
    { subject: "Calculus", prompt: "What is the derivative of a constant?" },
    { subject: "Concurrency", prompt: "What does a mutex protect?" },
    { subject: "Geometry", prompt: "How do you find the area of a circle?" },
    { subject: "Databases", prompt: "What makes a transaction atomic?" },
    { subject: "C++", prompt: "When should a function accept a const reference?" },
    { subject: "Calculus", prompt: "State the product rule for differentiation." },
    { subject: "Concurrency", prompt: "What is a data race?" },
    { subject: "Geometry", prompt: "What is the sum of a triangle’s interior angles?" },
    { subject: "Databases", prompt: "What does a unique constraint guarantee?" },
    { subject: "C++", prompt: "What does std::move actually do?" },
    { subject: "Calculus", prompt: "What is the derivative of e to the x?" },
    { subject: "Concurrency", prompt: "What is the purpose of a condition variable?" },
    { subject: "Geometry", prompt: "What makes two triangles similar?" },
    { subject: "Databases", prompt: "What does an index help a database find?" },
    { subject: "C++", prompt: "What is the purpose of a virtual destructor?" },
    { subject: "Calculus", prompt: "State the chain rule for differentiation." },
    { subject: "Concurrency", prompt: "What can cause a deadlock?" },
    { subject: "Databases", prompt: "How does a foreign key relate two tables?" }
]
function get(index) { return designs[Math.max(0, Math.min(designs.length - 1, index))] }
