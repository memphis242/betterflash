# GUI follow-ups

## Deck creation keyboard controls

- Keep Enter moving to the next field in the deck creation dialog.
- Make Ctrl+Enter submit and save the deck when at least a nonblank name is
  provided, subject to the dialog's normal validation.

## Deck deletion confirmation

- Make the **Delete deck** button red in the confirmation dialog to clearly
  identify the destructive action.

## Ask AI message presentation

Deferred for later:

- Render Markdown in chat messages for readable prose, lists, and code blocks.
- Present tool calls and sources as compact badges with brief summaries.
- Show a summary tooltip on hover and open a detail window when a badge is
  clicked.

## Typography

Evaluate where a monospace face helps and where a proportional face would make
the application easier to read. Keep the current app typography until that
comparison is ready. Inline Markdown code enclosed in single backticks must
remain visually distinct from surrounding prose, using a different monospace
face or a quiet background. The code renderer provides a background in this
iteration; broader font selection remains a separate design task.

## Shared menus

Soft rows is the selected shared menu treatment: inset rows, generous spacing,
rounded corners, quiet group dividers and clear focus/hover states. Apply it to
right-click context menus, three-dot menus and other shared action menus in both
themes. Atomicize uses an atom glyph; reset uses an empty-box glyph. Preserve
native keyboard navigation, disabled/destructive states and action tooltips.
