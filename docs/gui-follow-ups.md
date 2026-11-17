# GUI follow-ups

## Ready for manual review

Implemented 2026-11-17. Automated testing remains paused; these changes are ready
for the user's visual and interaction review.

### Deck dialogs

- Enter advances from name to description and from description to parent deck.
  Shift+Enter inserts a description newline. Ctrl+Enter submits through the normal
  validation when a name is provided; busy operations cannot submit again.
- Delete deck and Delete card confirmation buttons use the destructive red
  treatment, with contrasting text in each theme.

### Editor text menus and shared menus

- The card source editor and deck text fields use the shared soft-row context
  menu, with outlined editing glyphs and visible hover/focus states.
- Menu actions reflect selection, read-only state, and native undo/paste
  availability. Selection remains intact when opening the menu.
- Shared menus render inside the application window, avoiding separate native
  popup creation. Runtime latency has not been measured.
- Markdown selection menus use the same styling. Qt 6.9 or later is required.

### Check findings while editing

- Completed correctness and completeness results remain visible in a bounded
  panel beside the source/preview on wide windows, or below it on narrow windows.
- Findings include the claim, explanation, suggested adjustment, and source
  references. Each check mode can be dismissed separately.
- Findings are snapshots: editing the checked text marks them as referring to
  earlier text; a successful recheck replaces that mode's result. A clean deck
  recheck also replaces earlier warnings.
- Individual reports are retained for up to 48 card/draft contexts during the
  session, with a visible notice at the retention limit. The latest deck-check
  findings remain available independently of that limit, including after its
  modal closes. Reports do not change content or certify an edit as correct.

### Ask AI

- The model warning appears above the conversation and below the composer.
- Explicitly opening Ask AI starts with an empty conversation. An unused empty
  chat can be reused only when its context matches and it has no unsent draft;
  prior conversations and protected drafts remain selectable.
- Messages render selectable Markdown. Code and inline code remain distinct;
  model-generated HTML and arbitrary images are not loaded.
- Tool calls and sources use compact badges with hover summaries. Clicking
  opens a bounded read-only detail window; opening a source URL is a separate
  explicit action.
- Completed tool metadata is saved with the chat. Older chats remain readable,
  though tool badges cannot be reconstructed where no metadata was saved.

### Typography comparison

Settings > Appearance > Compare reading typefaces shows the same prose in IBM
Plex Mono and the system sans-serif face, with code and identifiers monospace in
both. The comparison reflows on narrow windows. App-wide font selection remains
at its current setting while the user evaluates the samples.
