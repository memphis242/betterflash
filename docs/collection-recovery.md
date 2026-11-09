# Collection loading and recovery

A malformed card is an individual repair problem. Loading a collection retains
all readable decks and cards and keeps an unreadable card visible with its saved
text, record identifier, cause, and a repair action. Reviews exclude unreadable
cards and schedules. Opening a collection never repairs, deletes, or resets
their content or learning records automatically.

Study guidance and review-session summaries are derived views. Their failure
produces a warning while the primary collection remains available. Invalid
review-history entries remain stored and are marked individually; session
summaries exclude them. Pending sync events with malformed payloads remain
stored, with a warning. Sync stops before uploading an invalid event.

A failed database open is distinct from an empty collection. The application
shows the specific error and collection path, offers a retry, and retains any
previously loaded snapshot. SQLite corruption that prevents reading primary
tables still requires restoring a known backup or database recovery.

## Repairing a saved card

Use **Show affected cards**, then open the card's editor. Check the detailed
cause and correct the fields. A content repair must retain existing review
directions and their schedules. Invalid narration metadata is discarded by the
explicit repair; valid attachments are retained. Damaged numeric schedule values
require an explicit schedule reset or restoration from a valid database copy.
An unsupported stored direction requires inspecting and repairing that direction
before resetting schedules.

Before a repair, deletion of unreadable content, damaged-schedule reset, or sync
application with unreadable records present, the database worker writes a
consistent SQLite recovery copy into the collection's `recovery/` directory.
The directory and files are private to the current user. If copying fails, the
operation stops without changing the records. The notice gives the copy's path.
Such repairs clear the session's Undo/Redo history because invalid original
records cannot be represented safely as ordinary undo actions; subsequent saved
actions remain undoable.

Existing collections undergoing a schema migration also receive a recovery copy.
Invalid schedule values and orphan references are retained for inspection rather
than silently normalized. Unconvertible legacy history remains in its original
table and is reported separately.

## Backups

Portable JSON export validates raw stored fields. If a damaged record cannot be
represented faithfully, export reports the affected record and stops before
writing the destination. A portable export is not a substitute for the raw
SQLite recovery copy when investigating damaged data. Preserve the collection's
media directory alongside SQLite copies to retain image and narration assets.

Never replace a collection database while BetterFlash is running. Preserve both
the original and recovery copy before attempting manual restoration.
