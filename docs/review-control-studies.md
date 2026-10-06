# Review control studies

Five native studies compare icon-only Defer to queue end, Later date, and
Return to active card controls around the approved Review ledger queue.

| # | Study | Footer placement | Size | Hover treatment |
|---:|---|---|---|---|
| 1 | Utility circles | Pair at the left | 36 px | Tinted circle and accent outline |
| 2 | Quiet tools | Pair at the center | 34 px | Accent glyph and short underline |
| 3 | Shared capsule | Pair in a centered capsule | 40 px | Inset surface around the hovered tool |
| 4 | Ledger split | Defer at the left, later date at the right | 32 px | Heavier square outline |
| 5 | Right dock | Pair beside the microphone at the right | 44 px | Raised surface with tinted hover |

Return stays at the right above the queue in its reserved row. It appears only
when the active card is outside the viewport and centers that card when clicked.
The five Return controls have a compact height of 28 or 30 px.

The studies share the app's line icon set, warm dark and paper light palettes,
and keyboard focus treatment. Tooltips describe each action. Defer and Later
date also show the default D and S bindings.

## Review the options

```sh
./scripts/run-desktop.sh --control-designs
```

Choose a numbered study, or press Alt+Left / Alt+Right. Move the pointer over an
icon to see its hover treatment and tooltip. Preview hover displays all three
hover treatments together. Tab focuses controls; Enter or Space activates them.
Reset preview or Ctrl+0 restores the active card outside the viewport, making
Return available for comparison.

The gallery uses a fixed sample queue and separate settings. Grading, defer,
and later-date clicks identify the preview action in the gallery footer. They
do not open or modify a collection. Return and timeline scrolling operate on
the sample queue.

## Capture the studies

Use the executable path printed by `scripts/build-desktop.sh`:

```sh
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  /path/to/build/betterflash \
  --export-control-designs /tmp/betterflash-control-studies
```

The export option is available with BUILD_TESTING. It runs a native interface
sweep and exports dark, dark hover, and light images for all five studies,
plus an overview, focused default and hover contact sheets, a full context
sheet, and report.json.
