# Review queue design studies

BetterFlash includes an isolated native QML gallery for comparing twelve queue
directions. Each study uses the same 24-card fixture, reviewed and pending
states, horizontal timeline, keyboard focus, reset action, and light/dark theme
switch. The gallery is a design study, not the production review queue.

| # | Study | Style and proportions | Body font | Card size / gap | Frame |
|---:|---|---|---|---:|---|
| 1 | Archive index | Ruled catalog header; compact serif card | Caladea 18 | 194 x 126 / 14 | Inset |
| 2 | Reading margin | Tall reading card with numbered margin | Caladea 19 | 204 x 162 / 20 | Open |
| 3 | Study folios | Generous study surface with recall footer | Cantarell 18 | 232 x 148 / 16 | None |
| 4 | Review ledger | Wide compact entry with outcome column | IBM Plex Mono 13 | 286 x 94 / 10 | Rules |
| 5 | Technical plate | Square instrument plate with corner marks | Adwaita Mono 14 | 218 x 140 / 12 | Inset |
| 6 | Course tabs | Notebook card with course label tabs | Cantarell 17 | 186 x 154 / 14 | Rules |
| 7 | Contact sheet | Dense double-framed cards for scanning | Cascadia Code NF 13 | 166 x 118 / 8 | Inset |
| 8 | Numbered notes | Open typography with a numbered margin | Caladea 19 | 244 x 152 / 22 | Open |
| 9 | Paper slips | Warm paper surface with notebook rhythm | Caladea 19 | 198 x 156 / 16 | Inset |
| 10 | Academic register | Spacious question field with academic header | Cantarell 17 | 248 x 142 / 14 | Rules |
| 11 | Study bookmarks | Tall narrow markers for queue visibility | Cantarell 16 | 144 x 194 / 14 | None |
| 12 | Recall record | Side-by-side question and recall mark | IBM Plex Mono 14 | 272 x 120 / 12 | Open |

The gallery sidebar selects a study and can collapse to a numbered rail. Previous
and next buttons and `Alt+Left` / `Alt+Right` change studies. The preview
timeline supports horizontal wheel scrolling, Left/Right keyboard focus,
Enter/Space selection, click selection, a Return to active card action when the
active card is off screen, and `Ctrl+0` or Reset preview to restore the sample
position. The gallery's 24 cards are native QML controls with text and state,
not image captures.

## Launching the gallery

The gallery uses its own settings namespace and does not construct an
`AppController` or open a collection store:

```sh
./scripts/run-desktop.sh --queue-designs
```

For a CI or artifact export, use the BUILD_TESTING executable directly. This
keeps the export isolated from the normal application data directory:

```sh
export BETTERFLASH_BUILD_DIR=/workspace/betterflash-build/queue-studies
./scripts/build-desktop.sh
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  "$BETTERFLASH_BUILD_DIR/betterflash" \
  --export-queue-designs /tmp/betterflash-queue-designs
```

The `--export-queue-designs` option is available in BUILD_TESTING builds. The
export captures the native QML gallery and verifies its study controls.
IBM Plex Mono is bundled. The other study typefaces use the system font lookup,
so Qt selects the available system fallback on another machine when a named
font is not installed.
