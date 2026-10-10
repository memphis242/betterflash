.pragma library

function makeDecks() {
    const definitions = [
        {id: "biology", name: "Biology", parent: "", label: "Biology", description: "From cells to systems: build a working understanding of living things.", summary: "The remaining cards connect membrane transport, organelle functions and neural signals. Start with the distinctions between passive and active transport, then revisit how neurons carry an impulse."},
        {id: "cells", name: "Cell biology", parent: "biology", label: "Cell\nbiology", description: "The structures and processes that keep a cell alive.", summary: "Compare the jobs of the major organelles and explain how substances cross a membrane. Several prompts ask you to distinguish closely related processes."},
        {id: "membranes", name: "Membranes", parent: "cells", label: "Membranes", newDue: 4, reviewDue: 7, later: 19, outcomes: [2, 3, 5, 14, 8], seconds: 10.8, description: "Transport, gradients and the cell boundary.", summary: "Review diffusion, osmosis and active transport. Four new cards introduce concentration gradients; the remaining prompts ask you to choose the transport mechanism for a short example."},
        {id: "organelles", name: "Organelles", parent: "cells", label: "Organelles", newDue: 2, reviewDue: 5, later: 13, outcomes: [1, 2, 3, 11, 7], seconds: 9.6, description: "", summary: "Match organelles to their roles in protein production and energy conversion. Two new prompts follow a protein from the ribosome to its destination."},
        {id: "physiology", name: "Physiology", parent: "biology", label: "Physiology", description: "How living systems coordinate their work.", summary: "The remaining review focuses on neural signaling: resting potential, the action potential and transmission across a synapse."},
        {id: "neurons", name: "Neural signaling", parent: "physiology", label: "Neural\nsignaling", newDue: 1, reviewDue: 4, later: 16, outcomes: [2, 1, 4, 9, 6], seconds: 14.2, description: "Follow a signal from one neuron to the next.", summary: "Explain the sequence of an action potential and the role of neurotransmitters at a synapse. One new card introduces the refractory period."},
        {id: "languages", name: "Languages", parent: "", label: "Languages", description: "Build everyday fluency through short, concrete prompts.", summary: "Spanish prompts contrast completed actions with ongoing past events. Japanese prompts focus on particles and short everyday sentences. Seven cards introduce new material."},
        {id: "spanish", name: "Spanish", parent: "languages", label: "Spanish", newDue: 3, reviewDue: 6, later: 26, outcomes: [2, 5, 6, 18, 12], seconds: 8.4, description: "Useful vocabulary and grammar in context.", summary: "Choose between the preterite and imperfect in short examples, then recall common travel phrases. Three new cards introduce irregular past-tense verbs."},
        {id: "japanese", name: "Japanese", parent: "languages", label: "Japanese", newDue: 4, reviewDue: 7, later: 19, outcomes: [3, 4, 5, 15, 10], seconds: 12.1, description: "Everyday sentences, particles and vocabulary.", summary: "Revisit the roles of the topic and subject particles, then complete short sentences about everyday routines. Four new cards introduce location and direction phrases."},
        {id: "computing", name: "Computing", parent: "", label: "Computing", description: "Reason about programs, ownership and algorithms.", summary: "The remaining cards cover C++ ownership and algorithmic complexity. Compare lifetime guarantees, then work through how time complexity changes with input size."},
        {id: "cpp", name: "C++", parent: "computing", label: "C++", description: "Language mechanisms and the guarantees they provide.", summary: "Explain ownership transfer, borrowing and object lifetime. The pending prompts use small code examples to distinguish owning pointers from non-owning references."},
        {id: "ownership", name: "Memory ownership", parent: "cpp", label: "Memory\nownership", newDue: 4, reviewDue: 3, later: 12, outcomes: [1, 3, 4, 13, 9], seconds: 15.3, description: "Make lifetime and ownership explicit.", summary: "Review unique ownership, shared ownership and the lifetime of references. Four new cards introduce move semantics and common dangling-reference pitfalls."},
        {id: "algorithms", name: "Algorithms", parent: "computing", label: "Algorithms", newDue: 2, reviewDue: 5, later: 18, outcomes: [2, 2, 5, 12, 8], seconds: 13.2, description: "Explain the reasoning behind common algorithms.", summary: "Compare search and sorting costs, and trace a binary search by hand. Two new prompts ask you to identify the invariant that makes a loop correct."}
    ];
    const byId = ({});
    for (const entry of definitions)
        byId[entry.id] = Object.assign({}, entry);
    function collect(id) {
        const node = byId[id];
        const children = definitions.filter(d => d.parent === id).map(d => collect(d.id));
        node.newDue = Number(node.newDue || 0) + children.reduce((n, d) => n + d.newDue, 0);
        node.reviewDue = Number(node.reviewDue || 0) + children.reduce((n, d) => n + d.reviewDue, 0);
        node.later = Number(node.later || 0) + children.reduce((n, d) => n + d.later, 0);
        node.due = node.newDue + node.reviewDue;
        node.total = node.due + node.later;
        node.variantTotal = node.total;
        const ownOutcomes = node.outcomes || [0, 0, 0, 0, 0];
        const ownCount = ownOutcomes.reduce((n, v) => n + v, 0);
        node.outcomes = ownOutcomes.map((n, i) => n + children.reduce((sum, d) => sum + d.outcomes[i], 0));
        node.reviewCount = node.outcomes.reduce((n, v) => n + v, 0);
        node.seconds = (Number(node.seconds || 0) * ownCount + children.reduce((n, d) => n + d.seconds * d.reviewCount, 0)) / Math.max(1, node.reviewCount);
        node.recall = (node.outcomes[1] * 0.5 + node.outcomes[2] + node.outcomes[3] + node.outcomes[4]) / Math.max(1, node.reviewCount);
        node.parentPath = node.parent ? path(node.parent) : "Top-level deck";
        return node;
    }
    function path(id) {
        const node = byId[id];
        return node.parent ? path(node.parent) + " / " + node.name : node.name;
    }
    definitions.filter(d => !d.parent).forEach(d => collect(d.id));
    return definitions.map(d => byId[d.id]);
}

function fromCollection(decks, cards, history) {
    const byId = ({});
    decks.forEach(d => byId[d.id] = d);
    function path(id) {
        const parts = [];
        const seen = [];
        while (byId[id] && seen.indexOf(id) < 0) {
            seen.push(id); parts.unshift(byId[id].name); id = byId[id].parentId;
        }
        return parts.join(" / ");
    }
    return decks.map(deck => {
        const scope = [deck.id];
        for (let i = 0; i < scope.length; ++i)
            decks.filter(d => d.parentId === scope[i] && scope.indexOf(d.id) < 0).forEach(d => scope.push(d.id));
        const cardIds = cards.filter(c => scope.indexOf(c.deckId) >= 0).map(c => c.id);
        const reviews = history.filter(r => cardIds.indexOf(r.cardId) >= 0);
        const outcomes = [0, 0, 0, 0, 0];
        reviews.forEach(r => { if (r.grade >= 0 && r.grade <= 4) ++outcomes[r.grade]; });
        return {
            id: deck.id, name: deck.name, label: deck.name, parent: deck.parentId || "",
            parentPath: deck.parentId ? path(deck.parentId) : "Top-level deck",
            description: deck.description || "", total: deck.cardCount || 0, variantTotal: deck.variantCount || 0,
            due: deck.dueCount || 0, newDue: deck.newDueCount || 0, reviewDue: deck.reviewDueCount || 0, later: deck.laterCount || 0,
            outcomes: outcomes, reviewCount: reviews.length,
            recall: reviews.reduce((sum, r) => sum + r.recallFraction, 0) / Math.max(1, reviews.length),
            seconds: reviews.reduce((sum, r) => sum + r.responseSeconds, 0) / Math.max(1, reviews.length),
            summary: deck.name === "BetterFlash examples"
                ? "The remaining prompts cover Markdown editing, recall grades and review scheduling. Revisit how deferring differs from postponing, then work through the examples of reversible and cloze cards."
                : "The pending prompts ask you to explain concepts in your own words and apply them to short examples. Start with the main distinctions, then recall the supporting details."
        };
    });
}

function layout(decks, variant) {
    const nodes = [];
    const groups = [];
    const byId = new Map();
    const children = new Map();
    for (const deck of decks) {
        if (!children.has(deck.parent)) children.set(deck.parent, []);
        children.get(deck.parent).push(deck);
    }
    let treeTop = variant === 1 ? 48 : 24;
    let width = 0;
    let depthCount = 0;
    for (const root of children.get("") || []) {
        const treeNodes = [];
        let leaf = 0;
        function visit(deck, depth) {
            const descendants = children.get(deck.id) || [];
            const firstLeaf = leaf;
            for (const child of descendants) visit(child, depth + 1);
            if (!descendants.length) ++leaf;
            const slot = (firstLeaf + leaf - 1) / 2;
            const node = {
                id: deck.id, deck: deck, depth: depth, root: !deck.parent,
                diameter: [160, 150, 144][Math.min(2, depth)],
                x: variant === 0 ? 120 + slot * 200 : 120 + depth * 300,
                y: treeTop + 88 + (variant === 0 ? depth * 220 : slot * 180)
            };
            depthCount = Math.max(depthCount, depth + 1);
            byId.set(node.id, node);
            treeNodes.push(node);
        }
        visit(root, 0);
        let bottom = treeTop;
        for (const node of treeNodes) {
            // Leave space for the branch control outside each circle.
            width = Math.max(width, node.x + node.diameter / 2 + 56);
            bottom = Math.max(bottom, node.y + node.diameter / 2);
            nodes.push(node);
        }
        groups.push({id: root.id, top: treeTop, bottom: bottom});
        // Each root owns its full expanded height, keeping later trees clear.
        treeTop = bottom + 72;
    }
    const edges = nodes.filter(n => n.deck.parent).map(n => ({from: byId.get(n.deck.parent), to: n}));
    return {nodes: nodes, edges: edges, groups: groups, depthCount: depthCount,
        width: Math.max(1, width), height: Math.max(1, treeTop - 40)};
}
