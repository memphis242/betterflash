# Deck checkpoints

## Create a checkpoint

Right-click a deck in the Library folder tree or the Review hierarchy and
choose **Checkpoint**. The dialog compares the selected deck and all its child
decks against the latest checkpoint. It lists added, modified, and deleted
cards, with expandable before/after content, and deck metadata changes in a
separate tab. The first checkpoint is an initial snapshot with every card new.

Enter an optional note and choose **Create checkpoint**. No language model is
needed. **Generate LLM summary of changes** is a separate, optional action using
the configured language model. It receives the full deterministic change list,
including before/after content, and requests two or three sentences. Remote
providers receive those changes only after this explicit action. Oversized
change lists are summarized in bounded batches and then merged into the final
annotation. A single change that cannot fit the selected model's context, or an
operation exceeding the work budget, is rejected rather than silently truncated;
a handwritten note is always sufficient. Closing or refreshing the dialog
cancels pending generation.

A checkpoint captures deck names/descriptions and hierarchy, card identifiers,
front/back Markdown, type, tags, partial-recall point counts, managed images,
per-variant narration, and narration alignment baselines. It does not capture
API keys, model settings, review history, or scheduling state. Assets are
verified by content hash and copied into a deduplicated checkpoint media cache
inside the collection's SQLite database.

## Restore content

Choose **Restore to known checkpoint** from the same deck menu. Its bounded
horizontal timeline has evenly spaced nodes, chronological ordering, and a
persistent scrollbar. Select a node to reveal its annotation beneath it. The
**Checkpoint details** tab shows its full note and summary. The Cards and Decks
tabs show the changes that restoring that checkpoint would make now.

Choose **Restore selected**, inspect the confirmation, then **Restore
checkpoint**. Restoring removes cards added after the selected checkpoint,
reinstates deleted cards, and restores changed content. Existing variants keep
their current due dates, difficulty, stability, and review counts. Reinstated
variants receive fresh schedules due now. Historical reviews are retained.
Independent reverse directions and clozes remain independently scheduled.

The current deck state is saved as an automatic recovery checkpoint in the
same transaction before content is replaced. Restore that recovery checkpoint
to recover the previous content. No general edit/review undo system is implied.
Old checkpoints can be removed from the Checkpoint details tab after a separate
confirmation. Deleting a checkpoint leaves current cards and review history
unchanged, and prunes cached assets no longer referenced by any checkpoint.
The selected root deck stays under its current external parent. Sync recreates
missing parent folders needed for that path, while preserving existing parent
folder metadata and cards outside the checkpoint. If a card or
child deck from the checkpoint has moved outside the selected subtree, restore
reports a conflict rather than changing content elsewhere. End an active review
before restoring content.

Preview tokens bind actions to the deck state displayed. Changes from another
window or sync invalidate the preview; refresh before submitting again. Restore
and its outgoing content events commit together. Failures retain the existing
deck and pending events. Verified media materialized before a failed transaction
may remain as unused files.

## Storage, backups, and sync

Schema 7 adds local `deck_checkpoints` and content-addressed `checkpoint_media`
tables. Checkpoints are immutable, survive restarts, and are retained if a deck
is deleted. This first UI accesses them through a live deck; a browser for
checkpoints of entirely deleted root decks is not provided yet.

Portable version 5 backups include checkpoint history and historical media,
including assets no longer referenced by live cards. Imports remain additive
and reject differing records with the same immutable checkpoint identifier.
Versions 2, 3, and 4 remain readable. Checkpoint order is preserved on import.

Checkpoint history does not sync between devices in this version. Restored
content does sync, including explicit resurrection of deleted cards/decks.
Upgrade every client before syncing restores; older clients retain their
permanent deletion protection. Restore upserts explicitly reassert every target
deck and card. Their `freshVariantIds` list identifies variants recreated on
the restoring device and establishes a new schedule boundary for those variants.
Other existing variants keep their local schedule and review owner. When a
nonfresh variant is absent on another device, its supplied restore schedule is
used; this resolves concurrent deletion without inventing an unrelated default.
Older review events cannot rewind a freshly restored schedule.

Restore existence boundaries distinguish the authoritative target subtree from
ancestor folders restored only as dependencies. An older remote deletion waits
for pending local restore events, then removes unrelated sibling branches while
preserving the restored content and later local work in that subtree. A newer
deletion still wins. An ambiguous move into a concurrently deleted parent is
reported as a sync conflict so the user can move the content back before retrying.

A snapshot and each saved change list are bounded to 64 MiB and 100,000 snapshot
records. Checkpoint media uses the existing 64 MiB per-operation media limit.
Portable backups retain the 128 MiB file and 64 MiB decoded-media limits, with
100,000 top-level records and 100,000 aggregate snapshot records. Errors explain
when a deck or collection exceeds a bound; no partial checkpoint is saved.

## Verification status

Compilation and source review accompany this implementation. Automated tests
remain paused at the user's request. When testing resumes, cover add/edit/delete
restores, nested deck moves, retained variant schedules and historical reviews,
checkpoint media retention, backup round trips, stale previews, failed
transactions, and deletion/resurrection sync ordering across devices.
