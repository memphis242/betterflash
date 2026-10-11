# Library and card timeline studies

The Library opens with a folder tree of decks. Choosing a deck opens its own
cards in the right pane; parent decks do not mix in cards from descendants.
The panes start at equal widths, can be resized, and retain their visibility
and width settings. The card pane is closed on first use.

- Click a card to select it. Double-click or press Enter to open its details.
- The plus icon in the card pane adds a card to the selected deck.
- Ctrl+L focuses deck search. Tree arrow keys navigate and expand branches.
- Drag the divider to resize the panes, or focus it and use Left/Right.
  Home restores equal widths.
- Card detail renders both Markdown sides, including images, code and math.
  Editing, atomicizing, resetting and deleting remain available there.

## Three timeline variants

1. **Review ledger:** outlined event tickets with dates, recall and response time.
2. **Recall trace:** a connected trace of recalled fractions, colored by verdict.
3. **Chapter ribbon:** alternating event annotations around a chronological rail.

Choose a variant in the card modal. A separate themed comparison window uses
explicitly illustrative events and never writes them into the collection:

```sh
./scripts/run-desktop.sh --card-timeline-designs
```

Each lane supports horizontal scrolling, a visible scrollbar, and Left/Right,
Home and End while focused. Events are equally spaced in chronological order;
distance does not represent elapsed time. Earlier pages can be loaded from the
lane header. The next-review marker uses the earliest real variant schedule;
there are no predicted future review outcomes.

## Data and limits

Card history is requested on demand from the database worker, in pages of 100
events. Reviews come from the card's full persisted history, independently of
the main History page's 200-record limit. Aggregate statistics cover all its
review records, including those retained after a development reset.

Successful reviews mean Hard, Good or Easy. Average recall includes partial
recall. The next-review countdown uses the earliest scheduled variant.

Save and edit markers use retained local card snapshots. Identical saves and
schedule-only resets do not add edit markers. The first save is labeled
**First saved**, with a tooltip identifying it as the first save on this device.
Original creation dates and edits from other devices are not guaranteed by
the existing collection format. Recorded age is measured from the earliest
known save or review, not an invented creation date. Import/export and sync do
not currently transport a complete lifecycle audit history.

This iteration was compiled and opened for visual review. Automated tests
remain paused at the user's request.
