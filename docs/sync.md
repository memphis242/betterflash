# Sync server

BetterFlash sync shares one collection per account between devices. The server
stores an ordered, durable event log and content-addressed attachments. Clients
remain usable offline and exchange saved changes when Sync is requested.

## Shared data and device preferences

| Shared within an account | Kept on each device |
| --- | --- |
| Deck hierarchy and descriptions; card text, tags, kinds, and point counts | Appearance, fonts, rendering, window and navigation state |
| Independent forward, reverse, and numbered-cloze schedules | Model/provider selections, acceleration, downloaded models |
| Review history, session metadata, rating corrections, postponements | Voice, microphone, narration playback, search and shortcut settings |
| Referenced imported images and saved narration recordings | Provider credentials, sync token, endpoint and last-sync display |
| Content deletions and explicit restoration operations | Checkpoint history and session undo/redo stacks |

Restoring a local checkpoint or undoing a collection mutation emits shared
content operations. Generated text saved into a card is card content; selecting
a model or opening a page never invokes inference or sends model preferences.

## Local service

Run the service with a bearer token of at least 24 characters:

```sh
export BETTERFLASH_SYNC_TOKEN="$(python3 -c 'import secrets; print(secrets.token_urlsafe(32))')"
python3 server/main.py --port 8765 \
  --database /workspace/betterflash-sync/collection.sqlite \
  --media /workspace/betterflash-sync/media
```

Configure `http://127.0.0.1:8765` in desktop Sync settings and enter the same token.
For an environment token, also export `BETTERFLASH_SYNC_ENDPOINT` as the exact
server endpoint (the origin or full `/v1/sync` URL). The app only uses
`BETTERFLASH_SYNC_TOKEN` for that normalized endpoint, including its path and
port. It never supplies that environment token when another endpoint is selected.
Environment credentials are explicitly read-only: token editing and removal
report that the variable must be changed or unset followed by an app restart.
To use editable keyring credentials, unset the environment token before starting
the app. Pending keyring writes/removals are drained for up to ten seconds at
endpoint replacement and shutdown; a stalled operation blocks endpoint changes
and reports `KEYRING_PENDING`. An operation still blocked at shutdown is reported
to the application log. Unlock the keyring and verify the credential before
restarting if that happens.

The server and provisioning tool require Python 3.11 or newer; the server also requires Pillow with PNG, JPEG, GIF, and WebP support.
Install the distribution's `python3-pillow` (Fedora) or `python3-pil` (Debian).
No graphical display is needed. Keep the decoder package updated with the host's
security updates. A missing Pillow installation fails startup before listening.

For multiple user accounts, omit `BETTERFLASH_SYNC_TOKEN` and start with an
immutable credential registry and data root:

```sh
python3 server/main.py --port 8765 \
  --accounts-file /etc/betterflash/accounts.json \
  --data-dir /workspace/betterflash-sync/accounts
```

Initialize a registry and a separate private token file with:

```sh
python3 server/provision.py --accounts-file /private/path/accounts.json \
  --token-file /private/path/device-token
```

The parent directories must exist. Both files are staged and synced with mode
`0600` before exclusive publication; existing paths are never overwritten.
Caught write, publication, and fsync failures roll back only files published by
that invocation, so a clean retry is possible. Concurrent invocations can publish
only one registry/token pair. The two destinations are not a single filesystem
transaction: after power loss or forcible termination during publication,
check both outputs and remove an incomplete new pair before retrying. Never run
provisioning against a registry used by a running service. The command prints no credentials. Copy
the registry to the server and enter the token from the private file in each
device's existing Sync settings. Generate high-entropy tokens; hashes cannot
make a human-chosen password safe. Distinct random tokens for individual devices
may be added as hashes under the same account to permit separate revocation.

The registry contains SHA-256 digests rather than bearer tokens. Each account
gets an independent SQLite database and media directory beneath its canonical
UUID, and token rotation is supported by listing multiple digests:

```json
{"accounts":[{"id":"11111111-1111-4111-8111-111111111111",
  "tokenSha256":["0000000000000000000000000000000000000000000000000000000000000000"]}]}
```

The registry is loaded at startup; restart the service after changing it. To
revoke the last device, retain the account ID and set `"tokenSha256": []`.
This disables that account's authentication without deleting its collection or
preventing other accounts from starting. Restore a valid digest and restart to
reenable access. Revocation is effective after restart, including in-flight
requests that were accepted by the old process.

An authenticated `GET /v1/info` returns the account's stable `collectionId`,
protocol version, and capabilities. Protocol 4 clients check this identity
before uploading attachments or events and bind it locally in the transaction
that applies the first successful response. Subsequent sync attempts to a
different account or replacement server database fail before uploading. Keep
the server identity when restoring its database. A device can join a new
account with a separate local collection; changing credentials is not an
account migration tool. An unbound collection's first sync trusts the account
selected by its endpoint and token, so verify them before the initial sync.

## Hosted deployment

Use a server reachable from both devices, with a public DNS name and HTTPS.
The native client permits plaintext HTTP only on localhost. Example
[systemd](../server/deploy/betterflash-sync.service) and
[Caddy](../server/deploy/Caddyfile) configurations are provided. Install the
server code under `/opt/betterflash/server/`, put the root-readable registry at
`/etc/betterflash-sync/accounts.json`, install the unit, replace the Caddy
hostname, and enable both services. systemd passes the registry through its
credential directory and grants write access only to the service state
directory. Never expose the backend port directly; keep its loopback binding.
The examples require systemd credential support and Caddy with the
`request_body` directive. Validate the proxy configuration on the host before
enabling it. Restrict access and storage usage at the host/proxy as appropriate.

Enter the same HTTPS origin and account token on Linux and the phone. Sync the
PC after authoring, sync the phone before reviewing, then sync it afterward.
Sync the PC again to see the phone's review history and due dates. Airplane-mode
reviews remain local until that device successfully syncs. Sync is currently
explicit; this change does not add background mobile synchronization or verify
the Android client on a physical device.

Back up each account database and its complete media directory together while
the service is stopped. Alternatively use SQLite's online backup API and a
consistent media snapshot. A plain copy of a live WAL database file can omit
committed events. Preserve the credential registry separately, and keep raw
device tokens out of collection backups. Restore the complete database,
including `server_metadata`, to keep existing devices bound. Event logs and
attachment history are retained without automatic pruning so an offline device
can catch up. Monitor disk usage; no log compaction or aggregate media quota is
implemented. For this private VPN/personal deployment, accounts are trusted
and there is no signup or public registration surface. Put the state directory
on a dedicated filesystem or filesystem quota with capacity reserved for the
host, and alert on free bytes and inodes. The application does not provide
per-account storage-capacity guarantees: a compromised device can exhaust that state
budget, including through unreferenced attachments. Before admitting mutually
untrusted accounts, use separate service instances/filesystem budgets or add
transactional account quotas for log bytes, media bytes, and attachment counts.
Do not delete historical attachments merely because current cards no longer
reference them; an offline device may still need them during replay.

The backend bounds a connection's network lifetime to 30 seconds, including
headers, body reads, and response writes, with a separate 20-second socket
inactivity timeout. Socket shutdown does not cancel decoding or an already
running database transaction; accepted work may finish after the peer disconnects,
and idempotent retries reconcile a commit whose response was lost. A slow trickle cannot extend that deadline. There are at
most 32 worker threads and four authenticated requests per account (including
all its device tokens). Excess account requests receive `429 ACCOUNT_BUSY`;
excess global requests receive `503 SERVER_BUSY`. Retry after existing requests
finish. Unauthenticated partial headers still consume global slots until their
deadline. Keep the backend loopback-only and protect the VPN/proxy boundary;
these bounds are not a substitute for public-edge connection/rate controls.

## Protocol and compatibility

Upgrade the server and every synced device together to protocol 4. Clients send
`X-BetterFlash-Protocol: 4`; the server rejects older clients with HTTP 426.
New clients require `/v1/info` to advertise `independent-schedules`,
`hierarchy-repair`, and `short-interval-schedules`. Mixing old
clients could overwrite review schedules with stale card snapshots.
Media uploads require `X-BetterFlash-Collection` to match the identity obtained
from `/v1/info`; the server checks it before accepting attachment bytes. Clients
also send this binding when downloading media.
Existing prototype logs remain readable, but legacy resets encoded only as card
snapshots cannot be distinguished from content edits during fresh replay. If
such resets matter, back up the current collection and explicitly reset or
postpone the affected schedules again using upgraded clients before adding a
new device. Never discard pending local changes to resolve a version error.

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
| `card.upsert` | source `card` and exactly its `variants`, used to initialize missing schedules; `freshVariantIds` explicitly identifies newly created or recreated schedules |
| `card.delete` | source `id` |
| `review.add` | initial `review` and its updated `variant` |
| `review.correct` | corrected `review` and recalculated `variant`, with `previousReview` and `previousVariant` snapshots; review identity, timestamp, response duration, and review count are preserved |
| `variant.upsert` | `variant`, including a postponed due date |
| `history.apply` | Flat `changes` list of `{entity, id, before, after}` compensations for decks, cards, variants, reviews, and review owners |

Undo and redo enqueue `history.apply` after a strict local expected-state check.
Remote changes follow server event order, with independent review history and
schedule ownership. Compensating card writes can restore deleted cards; deck
metadata writes restore only their folder. A change with `contextOnly: true`
supplies a missing variant or its owner without replacing an existing schedule.
Only target card states contribute media references. Large operations split at
dependency-complete card boundaries, with parent decks before child content and
deck deletions after it. Checkpoints themselves remain local. All clients and
the server must support this event before using synchronized undo. It is part
of the protocol 4 contract. See [undo and redo](undo.md) for the full policy.

Imported historical reviews can use `historyOnly: true` without changing a
schedule. Variant identifiers are UUID version 5 values derived from the source
card identifier and direction or cloze key. Nested payloads are validated before
events enter the server log.

Corrections cannot use `historyOnly`. A local correction requires the stored
review and variant to match the session's last saved result. Remote corrections
wait for pending writes and dependencies. Corrections to the same review follow
server event order while keeping its identity, timestamp, and response duration.
If an independent review has advanced the schedule, the historical rating is
corrected but that newer schedule is preserved, with `SYNC_REVIEW_CONFLICT`
reported. The cursor still advances so later sync pages can continue.

Install a client and server version supporting `review.correct` on every synced
device before using corrections; older clients reject the new event type.
For upgraded collections and imported backups without review ownership metadata,
an exact match to the correction's previous schedule establishes ownership.

Reviews may carry the optional, complete set `sessionId`, `sessionDeckName`, and
`sessionStartedAt`. These fields identify the selected deck's review run and are
immutable during a rating correction. All records with the same session ID must
agree on its deck name and start time; clients reject inconsistent records within
the import or sync transaction. Reviews without session metadata remain valid.

Upgrade every synced client to the session-history build (local schema 4) before
using tracked review sessions. Older clients accept review events but discard
session metadata, leaving their copies as estimated groups after an upgrade.
Protocol 4 requires session metadata support on every device. Upgrade the server
too, so it validates session fields and their preservation during corrections.

Media is uploaded to `PUT /v1/media/<sha256>.<png|jpg|jpeg|webp|gif|wav>` with a 20 MiB limit and authenticated, streamed back through `GET` or `HEAD`. The server validates the content hash and writes atomically. Images must fully
decode as their PNG, JPEG, WebP, or GIF filename extension and contain at most
32 million pixels, matching the native client's supported formats and pixel
limit. Only the first displayed animation frame is decoded; arbitrary animation
length cannot multiply validation work. Referenced files are also validated
before event admission and replay, with a bounded 256-entry metadata cache. Narration WAV files must contain nonempty, canonical 16 kHz mono signed 16-bit PCM audio of at most 5 minutes.
Upgrade all clients and the server to the five-minute narration build before
syncing recordings longer than 60 seconds; older builds reject those attachments.
Upload referenced assets before sending a card event. A card and a received page
can reference at most 64 unique media assets and 64 MiB of attached data. The server
splits event pages at those bounds, and the client validates a page before fetching
its assets. Damaged local copies are preserved under a `.damaged-UUID` suffix
when sync downloads a verified replacement.

If an older server already accepted invalid image bytes, appending a correction
cannot remove that earlier poisoned event. Stop syncing all devices and preserve
the complete server state. On a device with the complete collection, export a
portable backup and repair/remove invalid attachments until that backup imports
successfully into a separate local collection. Provision a new account and sync
that separate collection to its fresh server history. Import/join from separate
collections on the other devices only after validating the recovered data. Keep
the old account disabled and archived until every device is recovered. Do not
edit event hashes, delete individual log rows, or rewind existing client cursors.
A readable trusted backup plus a fresh collection/account is the administrative
recovery route; the server does not silently discard poisoned history.

Cards can contain a `narrations` object mapping a review variant and side, such
as `forward/front`, `reverse/back`, or `c1/front`, to a SHA-256 WAV filename.
Markdown `media:` references remain image-only. A narration change emits an
ordinary `card.upsert` without changing the review schedule. An incoming legacy
card event without `narrations` preserves compatible saved recordings; an explicit
empty object clears them. Upgrade the server and clients before using audio
narration, since older versions cannot transfer or play WAV attachments.

Cards can also contain `narrationFingerprints`, keyed by the same variant and
side. Each SHA-256 digest binds a recording asset to its effective question or
answer text. Recording and confirmation mutations preserve these baselines in
the same transaction as their outgoing card event. An incoming legacy event
without this field preserves a stored baseline only for the same attached
audio asset; it never marks changed text as confirmed. An explicit empty
object clears the baselines, leaving attached recordings unverified.

New collection backups use version 6 for short-interval schedules and deck
checkpoints, including historical image and narration assets. Narration text
baselines remain included. The application still imports version 2, 3, 4, and 5
backups. Provider credentials
remain excluded from every backup version.

Checkpoint history is local to a collection and travels in portable backups;
it is not uploaded as sync history. Restoring a checkpoint emits deck/card
content mutations in the same transaction as the restoration and its recovery
checkpoint. An explicit `restore: true` flag on an upsert authorizes bringing
back a deleted deck or card. Ordinary upserts retain deletion protection. All
clients should be upgraded before syncing checkpoint restores; older clients
ignore the flag and keep deleted records deleted. Checkpoint restoration does
not rewind review history or retained variant schedules. Newly created variant
identifiers are explicit in `freshVariantIds` on both ordinary edits and
restores. A recreated direction or cloze key starts a new schedule generation;
its server sequence blocks older deferred reviews while retaining their history. Restore dependency events recreate missing ancestor folders without
overwriting live ancestor metadata. See [deck checkpoints](checkpoints.md).

## Conflict behavior and scope

Content and review schedules have separate ownership. A PC card edit or move
does not replace any existing forward, reverse, or cloze schedule, including a
schedule advanced by offline phone reviews. New variant keys get initial
schedules; removing a key removes its active schedule and retains past history.
Explicit resets and postponements emit `variant.upsert` schedule mutations.

The client retains each review in history. Rating corrections update the same
history entry; correction events remain in the ordered event log. Concurrent
reviews preserve both records. Existing review-count and server-order guards
choose the retained schedule; this is not a mathematical replay combining two
offline learning sessions. A review correction cannot rewind a schedule owned
by a subsequent review. Conflicts can require ending an active review session
before grading that card again.

Concurrent edits of the same card or deck use server arrival order, independent
of device wall clocks. This is whole-record resolution; different text fields
are not automatically merged. The server retains both original events, records
detected stale-cursor content conflicts, and returns conflict metadata with the
affected sync page. The client reports a completed sync with content conflicts
and up to ten affected record IDs so the user can inspect the result. The total
count covers all conflicts even when the metadata list is truncated to 1,000
records per page. Notice counts and up to ten record IDs are committed with the
download cursor and retained in local collection metadata across canceled or
failed later pages, app restarts, and clean subsequent syncs. Notices remain
visible until marked read. Acknowledgment compares the displayed notice revision
inside the database transaction, so a new notice cannot be erased by an older
acknowledgment. Marking notices read changes only local metadata and emits no
sync event. Detection is conservative for long backlogs: a device's own
earlier upload can also be reported if its event has not yet reached that
device's download cursor. Copied device IDs do not suppress conflict notices.
Content edits and reviews of the same card
are independent and do not create content conflicts. Recovery currently requires
the server log, rather than a graphical conflict editor. In an account database,
`content_conflicts` identifies the incoming and previous event sequences; select
the corresponding `event_json` from `events` to recover either version, then
save the desired content through the app. Do not edit the log or rewind cursors.
Conflicts are detected for direct card/deck writes; compensating undo operations
also retain their own strict client expected-state checks.

The server projects deck parent relationships in arrival order. If two offline
moves would create a cycle, it retains the earlier accepted relationship and
appends a corrective `deck.upsert` that restores the losing deck's previous
parent while preserving its other metadata. The original event remains intact.
An ordinary move or history compensation into an already deleted parent is
repaired to the previous valid location, or the root if that location is unavailable. A repair can carry `restore: true` and
`restoreContainerOnly: true` to restore the affected folder without restoring a
whole subtree. Each deletion response wrapper carries authoritative
`deletionScopes`, keyed by deleted root ID, with `decks` mapping deck IDs to
parents and `cards` mapping card IDs to their decks. The server captures this
scope at the deletion's sequence and persists it in the event transaction;
client submissions cannot supply it. Clients store scopes with pending events
and apply them to local echoes as well as remote replay. Moving a deleted
member out afterward cannot evade its tombstone, and downloading the same log
in one page or many pages produces the same membership. Canonical event bytes
and identifiers remain immutable. Projection migration rebuilds scope records
from the ordered log without rewriting events.
Clients apply this correction even on the device that authored the losing move
and report `SYNC_HIERARCHY_RESOLVED`. The repair and its source event are committed
in the same server transaction; retries cannot duplicate repairs. A cyclic move
received before its repair is held durably while the download cursor advances,
so a page boundary cannot strand an offline device. Prototype logs receive
needed final-state hierarchy repairs when the projection is first initialized.

Child decks received before their parent are retained and retried when the parent
arrives. Direct local edits still reject hierarchy cycles. Deleting a
deck removes descendant cards, retains their review history, and emits explicit
delete events for every descendant card and deck. Remote clients may observe the
subtree disappearing across multiple pages until all events are applied.
Deletion tombstones prevent stale clients from resurrecting removed notes.
An ordinary card write that first places a live or new card into a deleted
deck is normalized into an immediate server-authored `card.delete` in the
same transaction. The original write is
preserved and the deletion precedes subsequent client events, so its tombstone
is identical for author echoes and any download page size. Retrying the source
write cannot duplicate this deletion. Projection migration adds missing
barriers only for rejected cards that remain deleted after the complete legacy
log; a later explicit restore is preserved.
Each generated deletion also records a content conflict with the affected card
ID at the deletion's sequence. It appears in that download page's conflict
notice, including on the authoring device, and remains in the client's retained
notices until acknowledged. The submitted card content and deletion remain in
the server log for recovery. Startup backfills missing conflict records for
existing generated deletions without rewriting events or duplicating notices
on replay or retries. A client whose cursor already passed such a deletion
does not receive a retroactive notice; inspect the retained server log when
recovering a collection synced with an earlier server build.
Unsynced local mutations stay protected until their echoes establish server
order. Dependency events that arrive before their notes are retained and retried.
A terminal page with unresolved dependencies reports `SYNC_DEPENDENCY` instead
of reporting a completed sync. The cursor and pending events remain durable;
update every client and the server, then retry. Preserve the database for
inspection if the dependency remains unavailable.

Deletion scope metadata participates in server page byte budgets. Normal pages
remain approximately 6 MiB; one large deletion may occupy its own larger page.
Clients accept at most 32 MiB per response. Scope metadata is limited to 24 MiB,
100,000 decks per scope, 200,000 total deck memberships, and 100,000 total card
memberships. Oversized scopes fail before the server commits the mutation with
`DELETION_SCOPE_LIMIT`; delete smaller subtrees and sync each before continuing.

Explicit undo/redo schedule changes, resets, postponements, and direction
creation establish schedule sequence barriers. A review ordered before such a
barrier can enter history but cannot replace that schedule. Content-only edits
retain existing schedules, including independent offline reviews.

Log compaction and replaying concurrent reviews into a combined schedule remain
future work. HTTPS terminates at the deployment proxy. The desktop stores its token
through Secret Service when available. Tokens and provider keys are excluded
from collection backups and sync events.
