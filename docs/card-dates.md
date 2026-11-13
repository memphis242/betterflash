# Card creation and modification dates

New cards record creation and last-modified times in UTC. Editing a card's text,
kind, partial-answer points, tags, deck, or narration updates its last-modified
time. Saving identical content and ordinary review or scheduling changes do not.
Undo, redo, and checkpoint restoration count as modifications when they change
card content. Creation time remains attached to the card identifier.

Existing cards from collections and older backups do not contain reliable
creation or modification times. Those times remain unknown rather than being
inferred from first synchronization or import. Editing an existing card records
its modification time; its creation time remains unknown. Legacy sync changes
without a modification time use the event time when they change existing content.

Creation and modification times are persisted with cards and included in sync
and portable exports. Older incoming card events or checkpoints that omit these
fields preserve a known creation date. Older BetterFlash versions may not
understand schema 10; update the app before opening an upgraded collection.
Clocks can differ across devices; observed modification times are kept
monotonic and never earlier than a known creation time.

Malformed saved date metadata does not hide the card or its review schedules.
The affected date is treated as unknown and the collection reports a warning.
Preserve a raw recovery copy before repairing damaged dates. Export and card
mutations that would discard damaged date metadata stop with an explanation.
