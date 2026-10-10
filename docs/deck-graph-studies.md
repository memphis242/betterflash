# Deck hierarchy studies

Open the native gallery:

```sh
./scripts/run-desktop.sh --deck-graph-designs --data-dir /workspace/betterflash-demo
```

The three tabs present the same hierarchy and selected deck:

| Study | Hierarchy | Character |
| --- | --- | --- |
| 01 Canopy | Parents above their children, separate roots across the top | Open branches and generous space between levels |
| 02 Branch ledger | Parents at the left, successive child levels in columns | Ruled tree groups and consistent depth alignment |
| 03 Orbit map | Parents nearer the center, descendants branching outward | A compact map of related deck families |

Decks are circular nodes. Selection uses a heavier outline; the path to the
selected node is highlighted. No deck title or separate statistic cards occupy
the graph area. The viewport supports dragging, scrollbars when needed, Fit,
zoom buttons and Ctrl+wheel zoom. Tab focuses nodes, Left/Right traverses decks,
Up selects the parent and Down selects the first child.

The overview shows roots and their immediate subdecks. Each branch has a +/−
control; double-clicking its node also opens or closes the branch. Expand all
reveals the complete hierarchy, and Overview returns to the compact view.

The selected node opens a right-hand pane by default. It can be collapsed,
reopened from the graph toolbar, or resized with its separator. The separator
also accepts Left/Right; Home restores its default width. Both side panes use
the same menu/dock button component, with the right pane's dock glyph mirrored
to match its edge. The pane contains:

- Exact due-now, new-due, reviewed-due and due-later counts, including subdecks.
- Source card and review-item totals; reverse directions and cloze groups count
  separately as review items.
- Average recall, outcome distribution and average response time. With a
  working collection these use the deck's records in the latest 200 collection
  reviews, not a lifetime aggregate.
- A preview of an automatic summary of remaining material.
- An editable deck description, using a modal editor.

The gallery is a design preview. Summary text is illustrative; no language
model requests are made. Description edits last only for the current window.
Start review opens an explanation of the intended action; ordinary reviews
continue in the main application. Without `--data-dir`, the gallery uses a
sample collection. With one, the Working decks / Sample decks control switches
between the actual hierarchy and a fully populated comparison dataset.

Use `--deck-graph-variant 1`, `2`, or `3` to open a particular tab directly.

## Working example hierarchy

The development collection in `/workspace/betterflash-demo` has thirty decks:
three roots and twenty-seven empty subdecks. The BetterFlash examples and C++
roots retain their existing cards; Biology is a new root. Creation used the
normal database worker, including sync events. A collection backup from before
the additions is stored at
`/workspace/betterflash-handoff/deck-graphs/before-hierarchy.sqlite`.

```text
BetterFlash examples
  Organization
    Deck structure
  Review practice
    Recall grades
    Scheduling
      Intervals
      Postponement
  Writing cards
    Cloze cards
    Markdown
      Code and math
      Images
C++
  Language
    Types and references
    Templates
  Memory
    Ownership
      Smart pointers
    Lifetimes
  Standard library
    Containers
    Algorithms
Biology
  Cell biology
    Membranes
    Organelles
  Physiology
    Neural signaling
```

The populated sample collection provides additional independent roots and
review histories so the details pane can be compared with both empty and
populated subdecks.
