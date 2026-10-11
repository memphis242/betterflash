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

## Calendar trace

The selected Recall trace uses an elapsed-time horizontal axis. Each event is
placed at its actual timestamp, and calendar ticks are aligned to local hours,
days, weeks, months or years. Unequal intervals remain unequal on screen.
The vertical axis shows recalled fraction, with review markers using the
recorded rating colors. Lines connect consecutive compatible review variants;
creation/save and edit markers appear along the calendar baseline.
Today and the next review are vertical lines, with warm accent and violet
colors respectively. Hover or select either line to inspect its timestamp.

Choose 1 day, 3 days, 1 week, 1 month, 1 year or All from the span selector.
Presets show recent history; a preset larger than the known extent uses All.
Use +/- or Ctrl+wheel to zoom around the current view or pointer. Wheel, drag
or use the persistent scrollbar to pan. Home and End reach the domain bounds;
Page Up and Page Down pan by a viewport fraction. Left and Right inspect
individual events. Hover shows full details, including overlapping events.
The pinned Today button brings the present back into view without changing the
selected zoom or inspected event. T does the same while the timeline has focus.
Calendar bounds still apply, so today may sit off center near an endpoint.

Earlier history loads as the viewport reaches it. The axis retains the full
known history extent while pages arrive, so dates do not move when additional
records are loaded. The next-review marker uses the earliest real variant
schedule, with a small amount of future calendar space. No future review
outcomes are predicted. The plot renders only a viewport-sized canvas.

The preview window uses explicitly illustrative history spanning more than a
year and never writes those events into the collection:

```sh
./scripts/run-desktop.sh --card-timeline-designs
```

The earlier Review ledger, Recall trace with evenly spaced events, and Chapter
ribbon studies remain in `qml/CardTimeline.qml` as design references. The Library
uses the calendar-scaled Recall trace.

## Data and limits

Card history is requested on demand from the database worker, in pages of 100
events. Reviews come from the card's full persisted history, independently of
the main History page's 200-record limit. Aggregate statistics cover all its
review records, including those retained after a development reset.

Successful reviews mean Hard, Good or Easy. Average recall includes partial
recall. Its color is the average recorded grade rounded to the nearest of the
five rating categories, so full recall consistently rated Hard remains distinct
from Easy. Without reviews, the statistic stays neutral.

Best time is the minimum recorded response duration among reviews graded Hard,
Good or Easy. It is calculated across the card's variants and full retained
review history, including records preceding a development reset. With no
qualifying review, no best time is shown. A valid zero-duration result is not
confused with missing data. This display does not change scheduling.

The next-review countdown uses the earliest scheduled variant.

Save and edit markers use retained local card snapshots. Identical saves and
schedule-only resets do not add edit markers. The first save is labeled
**First saved**, with a tooltip identifying it as the first save on this device.
Original creation dates and edits from other devices are not guaranteed by
the existing collection format. Recorded age is measured from the earliest
known save or review, not an invented creation date. Import/export and sync do
not currently transport a complete lifecycle audit history.

This iteration was compiled and opened for visual review. Automated tests
remain paused at the user's request.

## Deferred interaction recovery

Make it easy to return to the user's previous context after navigating away.
Retain selection, scroll position, zoom and pane state where appropriate, and
offer a clear return control when exploring far from the active context.

Plan undo for flashcard edits and review-session selections or actions. Exact
scope, presentation, keyboard bindings and persistence behavior remain to be
designed. Existing rating correction is not a complete undo system. This is a
requirement for later work, not an implemented feature of this Library pass.
