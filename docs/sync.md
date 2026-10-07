# Sync server

Run the local prototype with a bearer token of at least 24 characters:

```sh
export BETTERFLASH_SYNC_TOKEN="$(python3 -c 'import secrets; print(secrets.token_urlsafe(32))')"
python3 server/main.py --port 8765 \
  --database /workspace/betterflash-sync/collection.sqlite \
  --media /workspace/betterflash-sync/media
```

Configure `http://127.0.0.1:8765` in desktop Sync settings and enter the same token.
An app launched from the same shell can read `BETTERFLASH_SYNC_TOKEN` directly.
The service uses only Python's standard library and does not need pip packages.

The service binds to `127.0.0.1`, uses SQLite write-ahead logging with synchronous FULL commits, and accepts atomic batches of at most 100 events and 4 MiB. Each event is limited to 3 MiB. Clients send a device UUID and integer cursor to `POST /v1/sync`; the response includes accepted event IDs, events after that cursor (up to 1000 and approximately 6 MiB), the new cursor, and media references. Event IDs are idempotent. A successful retry acknowledges the same IDs even if they were already stored. Reusing an ID with different canonical JSON returns a conflict and rolls back the entire batch.

The request envelope is:

```json
{"deviceId":"11111111-1111-4111-8111-111111111111","cursor":0,"events":[]}
```

Each event contains `id`, `deviceId`, `type`, `payload`, and an ISO `createdAt`
timestamp with a UTC offset. The response contains `acceptedIds`,
`events: [{"seq": 1, "event": {...}}]`, `cursor`, `hasMore`, and `media`.
Clients apply a received page, acknowledge their outbox, and advance the cursor in
one local transaction. Interrupted requests can safely be retried. The optional
request field `hasMoreLocal` is client bookkeeping for batches limited by size.

| Event | Payload |
| --- | --- |
| `deck.upsert` | `deck` with name, description, identifier, creation time, and optional `parentId` (empty or absent means a top-level deck) |
| `deck.delete` | `id` |
| `card.upsert` | source `card` and exactly its independently scheduled `variants` |
| `card.delete` | source `id` |
| `review.add` | initial `review` and its updated `variant` |
| `review.correct` | corrected `review` and recalculated `variant`, with `previousReview` and `previousVariant` preconditions; review identity, timestamp, response duration, and review count are preserved |
| `variant.upsert` | `variant`, including a postponed due date |

Imported historical reviews can use `historyOnly: true` without changing a
schedule. Variant identifiers are UUID version 5 values derived from the source
card identifier and direction or cloze key. Nested payloads are validated before
events enter the server log.

Corrections cannot use `historyOnly`. A local correction requires the stored
review and variant to match the session's last saved result. Remote corrections
wait for pending writes and dependencies, then compare their preconditions.
A conflict preserves the current data and reports `SYNC_REVIEW_CONFLICT` while
consuming the event, allowing later sync pages to continue.

Media is uploaded to `PUT /v1/media/<sha256>.<png|jpg|jpeg|webp|gif>` with a 20 MiB limit and authenticated, streamed back through `GET` or `HEAD`. The server validates the content hash and writes atomically.
Upload referenced assets before sending a card event. A card and a received page
can reference at most 64 unique images and 64 MiB of attached data. The server
splits event pages at those bounds, and the client validates a page before fetching
its assets. Damaged local copies are preserved under a `.damaged-UUID` suffix
when sync downloads a verified replacement.

## Conflict behavior and scope

The client retains each review in history. Rating corrections update the same
history entry; correction events remain in the ordered event log. Concurrent note and
schedule changes follow server event order; they are not merged by an LLM.
Child decks received before their parent are retained and retried when the parent
arrives. A hierarchy cycle is rejected without advancing the cursor. Deleting a
deck removes descendant cards, retains their review history, and emits explicit
delete events for every descendant card and deck. Remote clients may observe the
subtree disappearing across multiple pages until all events are applied.
Deletion tombstones prevent stale clients from resurrecting removed notes.
Unsynced local mutations stay protected until their echoes establish server
order. Dependency events that arrive before their notes are retained and retried.

This prototype has one collection and one access token. HTTPS termination,
separate accounts, log compaction, and replaying concurrent reviews into a combined
schedule are future server work. The desktop stores its token through Secret
Service when available. Tokens and provider keys are excluded from collection
backups and sync events.
