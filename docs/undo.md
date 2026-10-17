# Undo and redo

BetterFlash keeps a single linear history of the five most recent meaningful
actions in the current application session. Undo moves back one action; redo
reapplies it. A new action after undo discards the remaining redo branch. Failed
operations and changes that leave the data unchanged do not consume a slot.
Browsing, searching, selecting cards, and scrolling do not consume slots.

Use Ctrl+Z to undo, and Ctrl+Y or Ctrl+Shift+Z to redo. The top-right buttons show
the next action in their tooltips and fade when unavailable. Appearance settings
can hide the buttons without disabling the shortcuts. The bindings are editable
in Keyboard settings. A focused textbox retains its own native draft undo/redo;
application history is unavailable while editing or confirming a modal action.

## Included actions

- Deck creation, renaming, descriptions, moves, and subtree deletion.
- Card creation, edits, moves, deletion, and accepted atomicization.
- Saved narration changes and confirmation that a recording matches its text.
- Review grades and corrections, postponement, queue-end deferral, starting and
  ending a review, revealing an answer, pausing, and resuming.
- Card/deck review-state resets, imports, and loading example data.
- Checkpoint creation, deletion, and restoration, including the automatic
  recovery checkpoint belonging to a restore.
- Appearance, keyboard, model, search, code rendering, and sync connection
  settings. Changes committed together by one control share an entry.
- Custom speech-test recordings, labels, and expected transcriptions.

Redo restores the saved result rather than repeating a model request or creating
new identities. A restored grade keeps its original review timestamp, response
duration, and due time. Undo restores its prior independent schedule and review
ownership. Review actions also restore the queue and active card, answer state,
pause state, and elapsed response time. Returning to a card resumes its timer
from that point rather than counting time spent elsewhere.

## Storage and synchronization

Database changes are captured as deltas of the affected rows on the database
worker thread. Cascades and grouped operations remain one user action. Local
undo and redo check the expected current values, apply changes, and enqueue
compensating sync events in one transaction. They never rewind the database
file, sync cursor, or outgoing journal. If a change from another device has
already reached an affected local record, undo reports a conflict and preserves
both the current data and history position. Changes made concurrently before
devices synchronize follow the existing server event ordering, as ordinary
card and deck edits do; the later synchronized edit wins. This is not a
cross-device merge or a server-authoritative conditional transaction.
Compensating card writes are explicit restores, so they can reinstate a card
deleted concurrently before that compensation reached the server. This differs
from an ordinary card update, which does not clear a deletion tombstone. A deck
metadata compensation restores its folder alone, not previously deleted child
decks or cards; those require their own content restores.
Deleting a deck on another device follows the same subtree cascade as an
ordinary deck deletion, including children created concurrently before that
deletion reaches the server.

The five-entry cursor and review context are held in memory. Compressed database
deltas use SQLite TEMP storage, which can spill to temporary files rather than
holding five large imports in RAM. This storage contains affected card/deck
content, and lives only for the connection's lifetime. Managed assets needed by
the entries are retained; custom speech recordings keep shared, bounded WAV
data so replacing a recording remains reversible. The history clears on exit,
is not included in portable collection backups, and is not itself synchronized.
Checkpoint history remains the persistent mechanism for returning to older
deck content.

The sync server and all clients must understand `history.apply` before syncing
undo/redo changes. Older clients do not support that event type.
Transport still limits a single card and its dependent review changes to one
3 MiB event. An unusually large before/after pair can exceed that limit; the
operation then reports `UNDO_CONFLICT` without changing the records or cursor.

## Operations outside the history

Reading files, export, search, model tests, downloads, playback, recording an
unsaved take, and asking a model for a proposal do not modify the saved library.
They do not create undo entries or repeat during redo. A proposal enters history
when its resulting card or checkpoint is saved. Undo cannot retract a request
already sent to a cloud provider or a file exported outside the library.

Raw credentials and operating-system key-store writes are excluded from undo
snapshots. Credential selection and secret replacement remain explicit secure
key-store operations; the history never copies API keys or tokens.

## Verification status

This first implementation is built and handed over for manual refinement.
Automated tests remain paused at the user's request. Once testing resumes,
prioritize branching/capacity, grouped cascades, review timing and ownership,
recording restoration, failed operations, sync ordering/conflicts, and native
textbox shortcuts before the later baseline test and bug-hunt phase.
