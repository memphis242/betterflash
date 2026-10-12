# Deck hierarchy studies

**Branch ledger** is the selected main-app Review layout. The gallery retains
both studies for reference and opens Branch ledger by default.

Open the native gallery:

```sh
./scripts/run-desktop.sh --deck-graph-designs --data-dir /workspace/betterflash-demo
```

The two tabs present the same hierarchy and selected deck:

| Study | Hierarchy | Character |
| --- | --- | --- |
| 01 Canopy | Parents above their children, independent trees stacked vertically | Open branches and generous space between levels |
| 02 Branch ledger | Parents at the left, successive child levels in columns | Ruled tree groups and consistent depth alignment |

Decks are circular nodes. Selection uses a heavier outline; the path to the
selected node is highlighted. No deck title or separate statistic cards occupy
the graph area. Nodes start at 100% size. Expanded branches grow the scrollable
canvas; each tree reserves its expanded width and height, and later trees move
down or up as needed. Expanding branches and resizing the pane do not change
zoom. The branch being opened stays in view when the scroll limits allow it.

Use the mouse wheel, trackpad, scrollbars or dragging to explore both axes.
Scrollbars stay visible whenever their axis overflows. Zoom buttons and
Ctrl+wheel change the node size; **100%** restores it. Each tab retains its own
scroll position, zoom and expanded branches while the window is open.
Tab focuses nodes, Left/Right traverses decks, Up selects the parent and Down
selects the first child. Keyboard selection scrolls its node into view.
Branch ledger names its columns Root deck, Children, Grandchildren,
Great-grandchildren, and further great-grandchild generations. These headings
stay pinned vertically while tracking horizontal scrolling.

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
- An editable deck description: its I-beam cursor indicates inline editing.
  Double-click, Enter, F2 or touch long-press starts editing in place.
  Ctrl+Enter saves, Escape cancels, and Enter inserts a newline. Compact Save
  and Cancel controls appear during editing. Drafts stay with their deck.

The gallery is a design preview. Summary text is illustrative; no language
model requests are made. Description edits last only for the current window.
Start review opens an explanation of the intended action; ordinary reviews
continue in the main application. Without `--data-dir`, the gallery uses a
sample collection. With one, the Working decks / Sample decks control switches
between the actual hierarchy and a fully populated comparison dataset.

Use `--deck-graph-variant 1` or `2` to open a particular tab directly.

## Main-app integration

The normal Review page saves descriptions through the database worker and
sync outbox. Its inline draft remains available if saving fails. The right pane is
collapsible, keeps its chosen width, and uses the same menu/dock controls as
the left navigation. Narrow windows show the deck tree or details at full
width; collapse details to return to the tree.

With a prepared local language model, selecting a deck automatically summarizes
its due review prompts after a short delay. An explicitly configured remote
provider is also supported. Results are cached in memory for
up to twelve deck/content/provider combinations, preventing repeat requests
when revisiting unchanged decks. Leaving the view or starting a review cancels
its pending request. The request uses due variants including descendants,
preserves cloze masking and reverse prompts, and follows the existing payload
limits. No review session is started to generate a summary. Errors appear in
the pane with a retry action; without a prepared local model or configured
remote provider, the pane points to Language model settings.

The main app uses actual generated summaries. Illustrative summaries and
window-local description edits are confined to the design gallery.

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
