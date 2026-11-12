# BetterFlash project instructions

These rules supplement the global `AGENTS.md`. Read them before changing the
project. Keep comments focused on current behavior, never on historical
implementation details. Never use an em dash, emoji, automatic agent
co-author attribution, or a remote session label in project text or commits.

## Architecture and data

- Use C++23, const-correct interfaces, checked external input, and assertions
  for internal invariants. Initialize objects to safe, meaningful defaults.
- Qt Quick and QML are the native UI. Do not add Electron or a browser engine.
- Database access belongs to the database worker thread. A mutation and its
  outgoing sync event must be persisted in one transaction before reporting
  success or advancing review.
- Preserve independent schedules for reverse directions and numbered clozes.
- Isolate malformed saved records. A single unreadable card, schedule, history
  entry, or derived view must not hide the rest of the collection. Preserve raw
  data and a recovery copy before repairs; surface the affected record and cause.
- Keep user credentials out of Markdown, SQLite collection tables, backups,
  sync payloads, logs, and model prompts. Only send the non-secret card content
  needed for a user-requested model operation.
- Imported images are stored by SHA-256. Never read arbitrary local files merely
  because a card contains a Markdown URL.
- Model inference must follow an explicit user action or an explicitly enabled
  interaction such as Voice Control. Opening the app, selecting a deck, or
  opening a pane must not generate a summary.

## GUI principles

GUI work must follow the GUI principles skill. Read
`/home/aalmo/.claude/skills/gui-principles/SKILL.md` before GUI changes. If that
path is unavailable, locate the equivalent `gui-principles/SKILL.md` before
editing. The warm dark theme is the default; keep navigation collapsible and
pinned, actions visible, and lists bounded.

- Use `AppTabButton` for standard tabs. Active tabs must be visually apparent
  before hover: stronger background and bold text. Inactive tabs are muted.
- Editable fields need a visible boundary, label, padding, and I-beam cursor in
  their idle state. Focus strengthens the outline. Clicking outside an active
  field, including empty space in nested scroll containers, ends editing while
  retaining its draft. Do not require another control to take focus.
- Prefer inline editing for existing text. Double-click or touch long-press may
  enter editing; Enter inserts a newline in multiline fields, Ctrl+Enter saves,
  and Escape cancels. Preserve drafts by record ID while navigating.
- A dropdown's opening control must toggle it closed as well as open. Editable
  dropdowns use one outer frame and must not draw a second textbox border.
- Provide a clear way to return to the previous context after exploration and
  preserve selection, scroll, zoom, and pane state where appropriate.

## Build and validation

- Build with `scripts/build-desktop.sh` using this checkout's own build
  directory. Do not share a CMake cache between worktrees.
- Running `scripts/build-desktop.sh` in the primary checkout or any project
  worktree is preauthorized. Do not ask for permission before running it.
- After every merge into `main`, and after completed direct updates to `main`,
  build the accepted changes and run `scripts/install-user-launcher.sh` from
  the primary checkout with its own build directory. Installation and launcher
  updates are preauthorized. Verify the installed executable and launcher;
  report installation failures instead of claiming the update is installed.
  Keep unfinished changes out of the installed snapshot.
- Development builds include reset controls and exclude test targets by default.
- The user has paused automated testing during visual refinement. Do not add or
  run tests until explicitly requested. Compile, inspect, format, and launch
  GUI changes for manual review as appropriate.
- When testing resumes, prefer focused behavioral tests and avoid assertions
  about incidental UI text or layout. Use `scripts/verify-desktop.sh` for the
  broader desktop check when requested.
- Android is a draft until an Android Qt kit builds it and a physical device
  verifies microphone permissions, storage, model loading, and lifecycle. Do
  not imply that background voice is ready.

## Collaboration and Git

- Inspect `git status` and `git worktree list` before editing. Until the user
  says otherwise, work directly on `main` in `/home/aalmo/projects/betterflash`,
  the primary checkout. Do not create worktrees by default. Coordinate file
  ownership for parallel work and preserve concurrent edits.
- Preserve foreign edits. Stage only named files belonging to this change; do
  not use `git add .` or `git add -A` in a shared checkout.
- The root agent owns Git writes in coordinated work. Commit and push completed
  work regularly, including direct work on `main` under the standing authorization.
- Git commands in this project's primary checkout and worktrees are preauthorized,
  including branch management, commits, pushes, and merges. Use judgment and do
  not ask for per-command permission. This does not authorize discarding foreign
  edits or overwriting another session's work. If the execution sandbox still
  requires approval, explain that constraint and request a reusable command
  approval where supported.
- Do not reset, stash, clean, or switch branches in a shared checkout while
  another session may be active.

## Current product behavior

- Undo and redo cover the shared last five saved application actions for the
  current session. New changes discard the unused redo branch. Text editors
  retain native draft undo. Never replay a model call during redo.
- Local models are the default where supported; cloud providers are optional
  BYOK connections. Models download on demand and expose preparation and test
  controls in Settings.
- Review supports Missed, Partial, Hard, Good, and Easy, queue deferral, date
  postponement, custom narration, and optional Voice Control.
- Reset deck and Reset card are development controls. They clear schedules and
  review counts while retaining content and past history.

## Documentation

Keep behavior notes close to the implementation in `docs/`. Update the relevant
document when a public workflow or compatibility constraint changes. The main
entrypoint is [README.md](README.md). The Android setup and staged plan are in
[docs/android-plan.md](docs/android-plan.md); the existing implementation draft
is [docs/android.md](docs/android.md).
