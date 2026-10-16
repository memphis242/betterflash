# BetterFlash project instructions

Follow the user's global AGENTS.md instructions. Never use an em dash, emoji,
automatic agent co-author attribution, or a remote session label.

- Use C++23, const-correct interfaces, checked external inputs, and assertions
  for internal invariants. The user has authorized sequential UI refinement in
  the primary checkout on main. Use isolated worktrees again for parallel work
  or backend development.
- Root owns git writes. Subagents edit only their assigned files. Stage named
  files, preserve foreign work, commit and push development branches regularly.
- Qt Quick is the native interface. Do not add Electron or a browser engine.
- Database access belongs to its worker thread. Persist a mutation and its
  outgoing event in one transaction before reporting success or advancing review.
- Model integrations default to managed local inference. Cloud providers are
  optional BYOK connections. Download on first use, expose preparation and model
  checks in Settings, cap inference resources, and release idle models. Keep keys out
  of Markdown, backups, SQLite collection tables, sync payloads, and logs.
- Run model inference only for a user-requested operation or an explicitly
  enabled interaction such as Voice Control. Selecting a deck, opening a pane,
  or launching the app must not generate a summary. Provide an explicit
  Generate summary action and render its result as Markdown.
- UI defaults to the warm dark theme. Keep left navigation collapsible and
  pinned, page actions visible, and lists bounded.
- Images imported by the user are stored by SHA-256. Never read arbitrary local
  files just because a card contains a Markdown URL.
- Preserve independent schedules for reverse directions and numbered clozes.
- Build with `scripts/build-desktop.sh`, using this checkout's own build directory.
  Development builds include review-reset controls and exclude test targets by
  default. The user has paused testing during visual refinement: do not add or
  run tests until requested. Build and launch GUI changes for manual review.
- When testing is requested, prefer targeted behavioral checks, not assertions
  about copy or incidental visual arrangements. Parallelize compilation.
- Android is a draft until an actual Android kit builds it and a device verifies
  microphone permissions and lifecycle. Do not imply background voice is ready.
- Launch the completed desktop build for the user from this worktree. Give a
  fallback command. Manual user confirmation remains part of GUI handoff.

## GUI interaction preferences

- Editable text fields are visibly bounded in their idle state, with a
  persistent label, distinct background or border, internal padding, and an
  I-beam cursor. Focus strengthens the outline; read-only states remain legible
  and visibly different. Hover is never required to discover an editable field.
- Clicking outside an active textbox, including empty space in nested scroll
  containers, ends editing and restores its idle or view styling. Keep the
  existing draft and save policy without submitting the surrounding form.
  Clicking inside the field or its scrollbars preserves editing; another
  control taking focus must not be required to leave editing.
- A dropdown's opening control also closes it. Keep that control inside the
  popup's dismissal boundary so an outside-press handler cannot close the popup
  before the same click reopens it. Use a single outer frame for editable
  dropdowns; their text editor does not draw a second textbox border.
- Default to editing existing text in place. The text area shows an I-beam
  cursor, and double-clicking turns that same area into an editor. Apply this
  to deck descriptions and comparable text fields throughout the application.
- Keep the idle text area free of add/edit buttons and instructional hover
  tooltips. Keep editing in its original context rather than opening a modal.
- Provide keyboard entry into inline editing, a way to save and cancel, and
  touch access. For multiline descriptions, Enter inserts a newline,
  Ctrl+Enter saves, and Escape cancels.
- Retain drafts when navigating between records. Bind each draft and pending
  save to its record ID. Discard drafts on explicit Cancel or after their
  matching save succeeds.
- Make returning to the active or previous context easy after exploration.
  Preserve selection, scroll, zoom and pane state where appropriate; provide
  explicit return controls such as the timeline's Today button.
- Treat undo for card edits and review-session actions as a planned interaction
  requirement. Its behavior remains to be designed; do not imply that a
  general undo system exists because individual rating corrections do.
