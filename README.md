# BetterFlash

A native C++23 and Qt Quick flashcard application, starting with Fedora Linux.

The desktop prototype keeps its collection in SQLite on the device. Markdown is
the source of truth for both sides of a card. The interface uses a warm aubergine
dark theme, a separate paper light theme, IBM Plex Mono typography, and a
collapsible left navigation pane whose icons remain available when collapsed.
Its four navigation buttons fill the pane in equal-height sections. Review uses
the Turn the card glyph; menu bars expand the pane and the dock arrow collapses it.
Expanded items center their icon and label above a short description, with the
whole group vertically centered in its section.

## Run on Fedora

```sh
sudo dnf install cmake ninja-build gcc-c++ \
  qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtsvg-devel \
  qt6-qtmultimedia-devel qt6-qtspeech-devel
./scripts/build-desktop.sh
./scripts/install-user-launcher.sh
./scripts/run-desktop.sh
```

Qt 6.7 or later is required. The first build downloads pinned dependencies for
native math rendering (MicroTeX and tinyxml2), code highlighting (KDE Syntax
Highlighting and Extra CMake Modules), and local semantic search (ONNX Runtime).
Neither a browser engine nor Electron is used.
Build output lives on `/workspace`, separate from the source checkout. Set
`BETTERFLASH_BUILD_DIR` to choose another build directory.
The run script updates that build before launching, so source changes in this
worktree are included even when an older executable already exists.
The user launcher points at this worktree and its build. It also supplies the
desktop entry used by native desktop portals; it refuses to overwrite a launcher
owned by another checkout.
PNG, JPEG, and GIF use Qt's built-in readers. Install `qt6-qtimageformats` for
WebP images, or export them to PNG before attaching.

### Local models

The application is local-first. Language-model settings default to Automatic:
desktop systems with at least 12 GiB RAM select Gemma 4 E2B Q4_K_M (about
3.1 GB), while smaller systems select LFM 2.5 350M Q4_K_M (about 229 MB).
Android targets the compact LFM profile. Native llama.cpp uses CPU execution or
an available GPU when automatic acceleration can safely fit the model; CPU can
be selected explicitly. Speech and embedding assets are also downloaded on
demand, verified, and tested with fixed known inputs. See [local model support](docs/local-models.md).

The first run is empty. Create a deck or explicitly load the example collection.
For an isolated demonstration:

```sh
./scripts/run-desktop.sh --data-dir /workspace/betterflash-demo --demo
```

## Cards and review

The Review page uses **Branch ledger**: circular deck nodes form a scrollable
forest, with columns for **Root deck**, **Children**, **Grandchildren**,
**Great-grandchildren**, and deeper generations. Column headings stay visible
while scrolling. Branches expand without shrinking the nodes. Double-click a
node to start its review, or right-click for the shared deck actions. An outlined
**R** badge marks a deck with due review items, including its descendants.

Selecting a deck opens its resizable right pane with due counts, recent review
performance, a remaining-card summary, and a description. The description area
uses an I-beam cursor; double-click to edit it in place. Enter, F2 and touch
long-press also begin editing. Ctrl+Enter saves, Escape cancels, and plain Enter
inserts a newline. Source and Preview tabs support Markdown, with a full-width
Save description button below the text box. Drafts stay with their deck when
navigating away.
Descriptions are saved to the collection and queued for sync.
Start review includes the selected deck's descendants;
an empty deck offers Add card instead. During a review, End review and Summarize
remaining cards sit beside Pause/Resume and are also available in the actions
menu. Defer-to-end and rescheduling remain available while paused, including
their keyboard shortcuts; neither action resumes the session.

The isolated [review queue design studies](docs/queue-design-studies.md) gallery
compares twelve native QML queue treatments without opening the collection.
Launch it with `./scripts/run-desktop.sh --queue-designs`.
The [review control studies](docs/review-control-studies.md) compare five icon
button treatments using `./scripts/run-desktop.sh --control-designs`.
Compare three progress-bar treatments together using
`./scripts/run-desktop.sh --progress-designs`. The shared slider previews the same
review count in all three bars; the theme button switches their palette.
The gallery uses sample outcomes and its own settings, without opening a collection.
Compare three steaming-teacup logo candidates alongside the original blueberry
using `./scripts/run-desktop.sh --logo-studies`. Each appears in a header bar
and at a larger size in the same themed window.
Compare three Review icons, five expand/collapse controls, and grouped versus
full-height navigation with `./scripts/run-desktop.sh --sidebar-designs`.
The two live sidebar previews expand independently and share the selected glyphs.
Compare two circular-node deck hierarchies with
`./scripts/run-desktop.sh --deck-graph-designs --data-dir /workspace/betterflash-demo`.
The tabs show **Canopy** and **Branch ledger**. Trees grow at a fixed node size
and stack vertically, moving later trees down as branches expand. Scroll or
drag to explore; Ctrl+wheel zooms and **100%** restores the default node size.
Select a node to open its resizable right-hand details pane.
Omit `--data-dir` for the populated sample collection. The gallery uses sample
LLM summary text and keeps description edits in its own window. It does not
call a model or start a real review. See [the deck graph studies](docs/deck-graph-studies.md).

### Library and card history

The Library starts with a folder tree of decks. Select a deck to open its card
overview on the right, then double-click a card (or press Enter) for its front,
back, review statistics and a scrollable history timeline. The plus icon in the
card pane creates a card. Both panes can collapse and their divider is resizable.

Deck toolbar controls add root decks or subdecks, expand or collapse the whole
tree, and search deck names with exact, prefix and spelling matches. Click blank
space in the deck tree to browse and search the entire library; each card then
shows its deck. Card pages fit the available window height, with navigation at
the top of the Cards pane. Ctrl-click adds individual selections and Shift-click
selects a contiguous range.

Search icons toggle their fields while preserving active queries. The dropdown
beside deck search reveals **Search within decks** to search card contents and
meaning; deck-name and card-content queries are combined with AND. Parent decks
include matches in their subdecks. The Cards pane's filter button adds tags,
card type and review-state restrictions, with Ctrl/Shift multi-selection for tags.

Drag a deck onto a row's center to make it a child, or near the row's edge to
place it alongside that deck. An indented insertion line shows the destination;
sibling decks remain alphabetically ordered. Drag selected cards onto a deck to
move them, or choose **Move card** from the shared card menu and type or select
the destination. Moves preserve review schedules and history.

Settings > Code rendering provides separate dark and light code themes, defaulting
to Iceberg Dark and Iceberg Light. Fenced
blocks have one continuous background; inline code has distinct styling.
Recognized language identifiers select native syntax highlighting. C and C++
also use an available `clangd` language server for semantic highlighting.
See [code rendering](docs/code-rendering.md) for configuration and current limits.

Shared action menus use the Soft rows treatment with inset rows and grouped
actions. Atomicize uses an atom glyph and reset uses an empty box.
Compare the original menu studies with `./scripts/run-desktop.sh --menu-designs`.
The gallery includes card and deck actions, light/dark modes, and menus opened
from a right-click or three-dot button. Actions in this gallery are previews.

Find a card now presents exact, prefix and substring matches first, followed by
typo-tolerant matches and related meanings. Semantic retrieval is enabled by
default and runs locally with a downloadable BGE model. Settings > Search can
disable it, select the desktop or compact model, rebuild its cache, or configure
a compatible BYOK embedding service. See [Library search](docs/search.md) for
model downloads, provider setup and ranking details.

The Recall trace plots events at their actual timestamps. Pan through retained
history and choose day, 3-day, week, month, year or full-history scales. Average
recall uses the card's typical rating color; Best time counts only Hard, Good
and Easy reviews. Open an illustrative calendar preview with:

```bash
./scripts/run-desktop.sh --card-timeline-designs
```

See [card timeline studies](docs/card-timeline-studies.md) for keyboard controls
and the limits of creation/edit history in existing collections.
Deferred ideas for weakness ranking and optional language-model study support
are recorded in [study support ideas](docs/study-support-ideas.md).

### Review history

The History page shows the latest three review sessions above a dated history
list. History defaults to the current local Monday-to-Sunday week. Controls beside
its title move the period backward or forward by seven days, select a single date
or an inclusive date range, and return to this week. Sessions are filtered by the
local date of their last answer, matching the timestamp shown in each row. The
recent three sessions and Learning statistics keep their own scopes.

Select a row to open a collapsible, resizable details
pane with the cards reviewed, overall recall, each rating's count, and response
times. The overall recall score is the mean recalled fraction; its color follows
the average rating, as it does in card details. Reverse directions and cloze
variants count as separate answers, with distinct source cards counted separately.

New reviews retain their session identity across restart, export and sync. Older
records without that identity appear as **Estimated session** groups: consecutive
reviews of the same deck, on the same UTC date, with no gap over 30 minutes.
Rating corrections update the totals without adding another answer or changing
its recorded time. Resetting or deleting a deck preserves its past reviews.

The learning overview below the lists combines a calendar heatmap with
provisional recall, coverage, consistency and trend statistics. Its filter scopes
the view to selected decks and their children, counting each card once; the
default is the entire library. Review
deck details include a compact heatmap, and **Heatmap** in Review or Library deck
actions opens the full calendar. See [learning overview](docs/learning-overview.md)
for the score formula and the treatment of unreviewed, moved and deleted cards.

### Development reset controls

Desktop builds made with `scripts/build-desktop.sh` include **Reset deck** on the
Review page's deck actions menu, and **Reset card** in the Library's selected
card details. Both are also available in the review actions menu. Deck resets
include all subdecks; card resets include every reverse and cloze variant.
A confirmation explains the scope. Resetting returns schedules to new and due
now, restores initial difficulty and stability, clears review counts, and ends
the active session after saving. Content, images, tags and past history remain.
Schedule changes use the normal sync outbox.

These controls are excluded by default in direct CMake builds. Disable them in
scripted builds with `./scripts/build-desktop.sh -DBETTERFLASH_DEVELOPMENT_TOOLS=OFF`.
Development builds exclude test targets unless `-DBUILD_TESTING=ON` is provided.

### Review behavior

- Edit Markdown, fenced code, inline `$...$` or display `$$...$$` math, and images
  on either side. Attached images are copied into the collection by content hash.
  Attach local copies of web images; card content does not trigger external image
  requests or read arbitrary local paths.
- Basic cards have one review item. Reversible cards and numbered cloze deletions
  have independent review schedules.
- Select **Create inverted form** in the card editor to add the reverse direction.
  Its question automatically begins with "Ask the question that this answers
  based on deck context: " followed by the original answer; its answer is the
  original question. Editing either side updates both directions.
- Browse decks horizontally on Review. The root shows top-level decks; enter a
  deck to browse its subdecks, or expand the selected deck's tree. The deck editor
  can place a deck under a parent. Reviewing a parent includes its descendants.
  Queue statistics count review variants, including each inverted direction and
  numbered cloze group separately.
- Review with Missed, Partial, Hard, Good, or Easy. Partial recall records the
  fraction remembered. Response time contributes a bounded adjustment.
- Review cards inside the main app, with a rounded question-and-answer
  container. Pause sits beside the deck title. The collapsible ledger queue has
  fading edges, rounded cards, bold
  numbers, thicker left rails, and a filled tag capsule below each preview. All tags
  appear above the question, with stable purple, orange, green, or red colors
  for each tag. The pause/play control sits beside the deck title. Upcoming cards become clearer as they enter view;
  card widths and gaps stay fixed. The active card glides into the center as you
  navigate or grade cards, preserving queue order. The persistent queue scrollbar
  and Return to active card
  control let you explore and return without changing the active card.
- The Cut ribbon progress bar above the queue reveals each reviewed card's recall
  outcome with transparent slanted separators. Ungraded slots share a continuous
  neutral track. Reduce motion in Appearance disables queue animation. Previous
  and next arrow bars span the sides of the main card. Choosing a grade saves it
  and advances to the next pending card. Left/Right, the arrow bars, and queue
  previews also let you navigate. Returning to a graded card and changing its
  result requires confirmation. Corrections preserve the original review time
  and response duration without adding another review. Partial recall opens an
  amount picker before recording the grade. A timer sits above the controls and
  a rounded Reveal answer cover sits below the prompt. Leaving Review pauses
  the session; Resume continues it. The final card stays open until Finish review.
- Defer moves the current item to the queue's tail without changing its schedule.
  Postpone chooses a future date and removes the item from the current queue.
  Their circular icon buttons sit together at the center of the review footer;
  voice stays at the right. These controls and Return use 44 px targets with
  descriptive tooltips, configured shortcut hints, and keyboard activation.
- Decks, source notes, review schedules, history, and images can be backed up and
  synchronized. API keys are excluded from collection data.

Adaptive v1 is an inspectable scheduling heuristic, not a claim of experimentally
optimized retention. See [the scheduling design](docs/scheduling.md).

## Keyboard

| Action | Default |
| --- | --- |
| Commands and deck picker | `Ctrl+K` |
| Find a deck | `Ctrl+L` |
| New card / new deck | `Ctrl+N` / `Ctrl+Shift+N` |
| Edit the selected card | `Ctrl+E` |
| Review / Library / History / Settings | `Alt+1` / `Alt+2` / `Alt+3` / `Alt+4` |
| Reveal answer | `Space` |
| Previous / next review card | `Left` / `Right` |
| Missed / Partial / Hard / Good / Easy | `1` / `2` / `3` / `4` / `5` |
| Defer / postpone / pause | `D` / `S` / `P` |
| Close review and pause | `Escape` |
| Insert or cross front/back separator | `Ctrl+Enter` |
| Turn selected text into a cloze | `Ctrl+Shift+C` |
| Attach an image | `Ctrl+Shift+I` |
| Save the card | `Ctrl+S` |

Bindings are configurable in Settings. Review keys are scoped to review; they
do not consume characters typed into editors or dialogs.
The deck browser also accepts Left/Right to choose a deck, Down to browse its
subdecks, and Up to return to the parent level. Tree rows use Left/Right to
collapse or expand their branches.
Left/Right selects the adjacent review card without reordering the queue.
The timeline can also be scrolled horizontally without recentering the current card.

## Voice and language models

Voice recognition, speech synthesis, and language-model features are local by
default. Desktop voice uses managed Whisper Turbo by default, with Vosk alternatives
and Piper/Sherpa speech output downloaded on demand;
the Settings page exposes progress, cancellation, and known-input validation.
Each model settings page has a **Location of model:** selector with Local and
Remote choices. Groq recognition, remote speech output, and other remote
providers remain explicit BYOK choices. Keys
are saved through the desktop Secret Service when available and never enter
collection data, backups, sync events, or logs. Click the API-key field or
**Reuse saved key** to reuse a key from another model settings page for the same
provider. Names such as
`speech-recognition-api-key` identify where it was entered. See
[API-key reuse](docs/api-keys.md), [voice setup](docs/voice.md)
and [local model support](docs/local-models.md).

The Review landing page offers **Generate summary** for a selected deck's due
prompts, including descendants. It uses the configured language model only
when requested and renders the result as Markdown. Unchanged results are cached
for the session; selecting a deck never runs the model. During a review,
**Summarize remaining cards** generates the remaining-queue summary on request.
Neither summary grades answers nor alters scheduling. See [deck summaries](docs/deck-graph-studies.md#main-app-integration).

Each saved card can also have recorded narration for its question and answer.
Recordings override automatic speech for that review direction. Automatic
speech skips code blocks. See [card narration](docs/voice.md#card-narration).

Atomicize proposes two through five focused cards from one saved note, or
recommends keeping a related list or procedure together. Review and edit the
proposal before explicitly replacing the original. It uses the same provider
configuration and retains the original's review history. See
[Atomicize behavior](docs/atomicize.md).

## Sync and Android

The local sync server is dependency free Python with SQLite and authenticated
media transfer. See [the sync protocol](docs/sync.md). VPS deployment is deferred.

The Android draft reuses the same native core and responsive QML. Its manifest
and build script are in `android/` and `scripts/build-android.sh`. A Qt Android
kit, Android SDK/NDK, and JDK are required to build an APK. See
[the Android draft](docs/android.md); background voice remains future work.

## Development

Sequential UI refinement currently uses the primary checkout on `main`, as
requested. Use isolated worktrees when parallel work or backend development
resumes. Each build checks that its CMake cache belongs to its own checkout.
Local tests are paused during visual refinement. When verification is requested:

```sh
./scripts/verify-desktop.sh
```

If `BETTERFLASH_BUILD_DIR` is unset, the build script prints the generated path.
Native GUI verification uses Qt events, including wheel and pane checks, instead
of browser testing. All test collections use separate temporary directories.
The Fedora workflow builds the native app and runs the same checks. Live provider
calls require your key and are excluded from automated verification.
