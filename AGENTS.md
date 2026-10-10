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
- Voice is BYOK by default. Local models are optional test tools. Keep keys out
  of Markdown, backups, SQLite collection tables, sync payloads, and logs.
- UI defaults to the warm dark theme. Keep left navigation collapsible and
  pinned, page actions visible, lists bounded, and editing in modal dialogs.
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
