# BetterFlash

BetterFlash is a native C++23 and Qt Quick flashcard application for Fedora
Linux, with an Android port planned next. The current desktop application is
version **v0.1.0-beta**. It is an experimental beta: collections and backups
are useful, but the scheduling heuristic and sync deployment still need broader
validation.

The app stores its collection in SQLite and uses Markdown as the source of
truth for both sides of a card. It has a warm aubergine dark theme by default,
a paper light theme, IBM Plex Mono typography, and a collapsible left
navigation pane. Qt Quick is used directly; the application does not use
Electron or a browser engine.

## Current capabilities

- Create nested decks and cards with Markdown, images, fenced code, inline and
  display LaTeX, tags, inverted directions, and numbered cloze cards.
- Review with Missed, Partial, Hard, Good, and Easy grades. Partial recall and
  response time contribute to the current adaptive scheduling heuristic.
- Defer a card to the end of the queue or postpone its next review date.
- Review in the main application with a timeline queue, progress bar, keyboard
  controls, voice control, custom narration, and optional speech models.
- Browse cards and decks in Library, inspect card history on a calendar trace,
  and view review history with learning statistics and a calendar heatmap.
- Search cards using exact, prefix, substring, fuzzy, and optional local
  semantic retrieval. Search deck names with exact, prefix, and fuzzy matching.
- Rank weak cards using deterministic recall, difficulty, trend, and response
  time evidence. Optional language-model help explains a card when requested.
- Generate a similar card, atomicize a card, summarize a deck, and correct
  speech transcripts only after an explicit user action.
- Create and restore deck checkpoints with deterministic change summaries and
  optional language-model annotations.
- Undo and redo the last five saved application actions with Ctrl+Z, Ctrl+Y, or
  Ctrl+Shift+Z. New changes discard the unused redo branch.
- Sync collection content, review schedules, history, and managed media across
  devices through an account-isolated server. Model and appearance preferences
  stay local. See [server setup and conflict policy](docs/sync.md).

## Install and run on Fedora

Install the desktop prerequisites:

```sh
sudo dnf install cmake ninja-build gcc-c++ \
  qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtsvg-devel \
  qt6-qtmultimedia-devel qt6-qtspeech-devel
```

Build and launch from this checkout:

```sh
./scripts/build-desktop.sh
./scripts/run-desktop.sh
```

The first build downloads pinned native dependencies, including math/code
rendering libraries, llama.cpp, ONNX Runtime and the desktop Whisper helper.
Qt 6.7 or later is required; a C++23-capable compiler is also required. The
build places generated files under `/workspace`
by default. Set `BETTERFLASH_BUILD_DIR` to choose another build directory.
The run script updates the selected build before launching, which avoids
accidentally running an older executable. The optional launcher integration is:

```sh
./scripts/install-user-launcher.sh
```

For a populated isolated example collection:

```sh
./scripts/run-desktop.sh --data-dir /workspace/betterflash-demo --demo
```

PNG, JPEG, and GIF work with Qt's built-in readers. Install
`qt6-qtimageformats` for WebP support. Imported images are copied into the
collection by SHA-256; Markdown cannot cause arbitrary local files or remote
URLs to be read.

## Local and remote models

Managed local inference is the default. Language-model settings select Gemma 4
E2B on larger desktop systems and LFM 2.5 350M on smaller systems. Speech and
embedding models are downloaded on demand, verified with known inputs, and can
be tested in Settings. Models are loaded when needed and released when idle.

Cloud providers are optional BYOK connections. API keys are stored through the
desktop Secret Service when available and never enter collection tables,
backups, sync events, Markdown, or logs. Keys can be reused by provider and
purpose, with names such as `speech-recognition-api-key`.

Voice Control uses a key binding by default. Press `M` to capture one
utterance; after speech begins, about 900 ms of silence submits it and stops the
microphone. Press `M` again to cancel. Always-on microphone review remains future work. Recorded
question and answer narration takes precedence over generated speech, and code
blocks are skipped by automatic narration.

See [local model support](docs/local-models.md), [voice setup](docs/voice.md),
[API-key reuse](docs/api-keys.md), and [transcript flow](docs/transcript-flow-studies.md).

## Desktop development

The source layout is organized by responsibility:

| Directory | Purpose |
| --- | --- |
| `src/core` | SQLite collection, cards, decks, scheduling, history, checkpoints, undo, and sync data |
| `src/integrations` | Model providers, summaries, secure keys, media, and external services |
| `src/voice`, `src/models`, `src/render` | Audio, local inference, and native Markdown/LaTeX rendering |
| `src/search` | Text and semantic search controllers and local embedding support |
| `qml` | Qt Quick pages, dialogs, controls, themes, and visual studies |
| `android` | Android manifest and platform draft |
| `docs` | Architecture, behavior, model, sync, and design notes |
| `scripts` | Reproducible desktop, Android, launcher, and model workflows |

Read [AGENTS.md](AGENTS.md) before changing the project. It contains the
project-specific architecture, security, GUI, worktree, build, and validation
rules. The [architecture guide](docs/architecture.md) explains thread and
storage boundaries.

Use isolated worktrees for parallel or backend work. Sequential UI refinement
may use the authorized primary checkout. Inspect `git status` and
`git worktree list` first, and stage only files owned by your change.

Build output is intentionally separate from source. Use the checkout's own
build directory and do not share a CMake cache between worktrees.

## Keyboard defaults

| Action | Default |
| --- | --- |
| Command palette and deck picker | `Ctrl+K` |
| Find deck | `Ctrl+L` |
| New card / new root deck | `Ctrl+N` / `Ctrl+Shift+N` |
| Edit selected card | `Ctrl+E` |
| Review / Library / History / Settings | `Alt+1` / `Alt+2` / `Alt+3` / `Alt+4` |
| Reveal answer | `Space` |
| Previous / next review card | `Left` / `Right` |
| Missed / Partial / Hard / Good / Easy | `1` / `2` / `3` / `4` / `5` |
| Defer / postpone / pause | `D` / `S` / `P` |
| Undo / redo | `Ctrl+Z` / `Ctrl+Y` or `Ctrl+Shift+Z` |
| Save card | `Ctrl+S` |

Bindings are configurable in Settings. Review shortcuts are scoped to review
and do not consume text typed in editors or dialogs.

## Android draft

Android is the next platform target. The initial reference device is a Samsung
Galaxy S26 Ultra running One UI 9.0 and Android 17. The Android target is a
draft until an Android Qt kit builds it and a physical device verifies storage,
microphone permissions, audio lifecycle, model loading, and synchronization.
Background voice is not ready.

The current manifest and draft build script are in `android/` and
`scripts/build-android.sh`. The setup, compatibility questions, device steps,
and staged implementation plan are in [docs/android-plan.md](docs/android-plan.md)
and [the Android draft notes](docs/android.md). Deployment to the VPS is out of
scope for the initial client work.

## Documentation map

- [Scheduling](docs/scheduling.md): current adaptive review heuristic.
- [Search](docs/search.md): ranking tiers and semantic retrieval.
- [Study guidance](docs/study-guidance.md): recall strength and evidence.
- [Checkpoints](docs/checkpoints.md): content snapshots and restore behavior.
- [Undo and redo](docs/undo.md): five-action history and sync behavior.
- [Sync protocol](docs/sync.md): collection and media synchronization.
- [Code rendering](docs/code-rendering.md): Markdown code themes and limits.
- [Atomicize](docs/atomicize.md): proposed focused-card workflow.
- [Roadmap](docs/roadmap.md): deferred desktop and platform work.

## Validation policy

The project is in active visual refinement. Automated tests are paused until
the user explicitly requests the testing and bug-hunt phase. During this pause,
compile and launch GUI changes for manual review, but do not add or run test
suites. When verification is requested, prefer focused behavioral checks first
and use:

```sh
./scripts/verify-desktop.sh
```

Live provider calls require the user's own key and are not part of automated
verification. Do not claim a device or model has been tested unless it was
actually run.

## License and status

BetterFlash is an experimental personal project. Licensing and public release
terms have not yet been finalized. The desktop beta is the current
development target; Android, Windows, and iOS remain future platform work.
