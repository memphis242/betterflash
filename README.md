# BetterFlash

A native C++23 and Qt Quick flashcard application, starting with Fedora Linux.

The desktop prototype keeps its collection in SQLite on the device. Markdown is
the source of truth for both sides of a card. The interface uses a warm aubergine
dark theme, a separate paper light theme, IBM Plex Mono typography, and a
collapsible left navigation pane whose icons remain available when collapsed.

## Run on Fedora

```sh
sudo dnf install cmake ninja-build gcc-c++ \
  qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtsvg-devel \
  qt6-qtmultimedia-devel qt6-qtspeech-devel
./scripts/build-desktop.sh
./scripts/install-user-launcher.sh
./scripts/run-desktop.sh
```

The first build downloads two small, pinned source dependencies for native math
rendering: MicroTeX and tinyxml2. Neither a browser engine nor Electron is used.
Build output lives on `/workspace`, separate from the source checkout. Set
`BETTERFLASH_BUILD_DIR` to choose another build directory.
The run script updates that build before launching, so source changes in this
worktree are included even when an older executable already exists.
The user launcher points at this worktree and its build. It also supplies the
desktop entry used by native desktop portals; it refuses to overwrite a launcher
owned by another checkout.
PNG, JPEG, and GIF use Qt's built-in readers. Install `qt6-qtimageformats` for
WebP images, or export them to PNG before attaching.

The first run is empty. Create a deck or explicitly load the example collection.
For an isolated demonstration:

```sh
./scripts/run-desktop.sh --data-dir /workspace/betterflash-demo --demo
```

## Cards and review

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
- Review cards in a rounded modal over the deck page. Pause sits directly beside
  the bounded deck title. The queue opens as a centered, horizontally scrollable
  timeline with fading edges; it has no rail or current label, and the current
  card may scroll out of view when the queue is long. Upcoming cards disclose
  more text and become clearer as they enter the view, with fixed card widths
  and gaps. The timeline includes all pending and completed cards in the session.
  Scrolling stops with the first or final card at the center. Click a pending
  preview to center and review it in place, keeping the queue order, or click a
  completed preview to inspect its answer without
  recording another review. A timer sits above the controls, and a large
  rounded Reveal answer cover appears below the prompt. Closing the modal pauses
  the session; Resume continues the same card and queue.
- Defer moves the current item to the queue's tail without changing its schedule.
  Postpone chooses a future date and removes the item from the current queue.
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
| Missed / Partial / Hard / Good / Easy | `1` / `2` / `3` / `4` / `5` |
| Defer / postpone / pause | `D` / `S` / `P` |
| Close the review modal and pause | `Escape` |
| Insert or cross front/back separator | `Ctrl+Enter` |
| Turn selected text into a cloze | `Ctrl+Shift+C` |
| Attach an image | `Ctrl+Shift+I` |
| Save the card | `Ctrl+S` |

Bindings are configurable in Settings. Review keys are scoped to review; they
do not consume characters typed into editors or dialogs.
The deck browser also accepts Left/Right to choose a deck, Down to browse its
subdecks, and Up to return to the parent level. Tree rows use Left/Right to
collapse or expand their branches.
In the review modal, Tab focuses the review timeline; Left/Right moves through
timeline cards without changing the review queue. Enter or Space selects the
focused preview. The timeline can also be scrolled horizontally without
recentering the current card.

## Voice and language models

Voice recognition uses bring-your-own-key Groq by default. Add the key in Voice
settings. Desktop keys are saved through the desktop Secret Service; when the
keyring is unavailable, they remain in memory for the session. Card content and
API keys never go to the sync server together.

Speech synthesis initially uses the system Qt speech engine. A remote TTS
provider can be added once selected. The optional local recognition setup is a
test facility, not a required app dependency. See [voice setup](docs/voice.md).

The optional remaining-card summary sends the remaining question prompts to a
configured chat-completions provider only when requested. It does not grade
answers or alter scheduling. Groq is the initial default; the endpoint and model
can be changed in Settings.

Atomicize proposes two through five focused cards from one saved note, or
recommends keeping a related list or procedure together. Review and edit the
proposal before explicitly replacing the original. It uses the same provider
configuration and retains the original's review history. See
[Atomicize behavior](docs/atomicize.md).

## Sync and Android

The local sync server is dependency free Python with SQLite and authenticated
image transfer. See [the sync protocol](docs/sync.md). VPS deployment is deferred.

The Android draft reuses the same native core and responsive QML. Its manifest
and build script are in `android/` and `scripts/build-android.sh`. A Qt Android
kit, Android SDK/NDK, and JDK are required to build an APK. See
[the Android draft](docs/android.md); background voice remains future work.

## Development

Work in an isolated git worktree. Each build checks that its CMake cache belongs
to its own checkout. For focused verification:

```sh
./scripts/verify-desktop.sh
```

If `BETTERFLASH_BUILD_DIR` is unset, the build script prints the generated path.
Native GUI verification uses Qt events, including wheel and pane checks, instead
of browser testing. All test collections use separate temporary directories.
The Fedora workflow builds the native app and runs the same checks. Live provider
calls require your key and are excluded from automated verification.
