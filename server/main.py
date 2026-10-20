#!/usr/bin/env python3
"""Local, single-account event and attachment transport for BetterFlash."""

import argparse
from contextlib import closing
from datetime import datetime, timezone
import hashlib
import hmac
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import math
import os
from pathlib import Path
import re
import sqlite3
import stat
import tempfile
import threading
from types import MappingProxyType
from urllib.parse import urlsplit
from uuid import UUID, uuid4, uuid5

TOKEN = os.environ.get("BETTERFLASH_SYNC_TOKEN", "")
EVENT_LIMIT = 100
RESPONSE_LIMIT = 1000
BODY_LIMIT = 4 * 1024 * 1024
EVENT_BYTES = 3 * 1024 * 1024
RESPONSE_BYTES = 6 * 1024 * 1024
MEDIA_LIMIT = 20 * 1024 * 1024
NARRATION_SAMPLE_RATE = 16000
NARRATION_CHANNELS = 1
NARRATION_BYTES_PER_SAMPLE = 2
NARRATION_MAX_SECONDS = 5 * 60
NARRATION_MAX_PCM_BYTES = NARRATION_SAMPLE_RATE * NARRATION_CHANNELS * NARRATION_BYTES_PER_SAMPLE * NARRATION_MAX_SECONDS
PAGE_MEDIA_LIMIT = 64 * 1024 * 1024
PAGE_MEDIA_COUNT = 64
MAX_WORKERS = 32
MEDIA_RE = re.compile(r"^/v1/media/([0-9a-f]{64}\.(?:png|jpg|jpeg|webp|gif|wav))$")
EVENT_TYPES = frozenset(("deck.upsert", "deck.delete", "card.upsert", "card.delete", "review.add", "review.correct", "variant.upsert", "history.apply"))
MEDIA_REFERENCE = re.compile(r'''media:([^\s\)\]>"'`]+)''')


class ProtocolError(Exception):
    def __init__(self, code, message, status=400):
        super().__init__(message)
        self.code, self.message, self.status = code, message, status


class Conflict(ProtocolError):
    def __init__(self):
        super().__init__("EVENT_CONFLICT", "An event identifier already contains different data.", 409)


class Accounts:
    """Immutable account credentials and stores loaded before serving requests."""

    def __init__(self, path, data_dir):
        try:
            with Path(path).open("rb") as source:
                document = decode_json(source.read())
        except (OSError, ValueError, UnicodeError, RecursionError) as failure:
            raise SystemExit(f"SYNC_ACCOUNTS: Cannot read a valid accounts file: {failure}") from failure
        if not isinstance(document, dict) or set(document) != {"accounts"} or not isinstance(document["accounts"], list):
            raise SystemExit("SYNC_ACCOUNTS: The accounts file must contain only an accounts array.")
        root = Path(data_dir).resolve()
        stores, digests = {}, {}
        for account in document["accounts"]:
            if not isinstance(account, dict) or set(account) != {"id", "tokenSha256"}:
                raise SystemExit("SYNC_ACCOUNTS: Each account must contain only id and tokenSha256.")
            account_id = account["id"]
            if not valid_uuid(account_id) or account_id in stores:
                raise SystemExit("SYNC_ACCOUNTS: Account identifiers must be unique canonical UUIDs.")
            account_digests = account["tokenSha256"]
            if (not isinstance(account_digests, list) or not account_digests
                    or any(not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest)
                           for digest in account_digests)
                    or len(set(account_digests)) != len(account_digests)):
                raise SystemExit("SYNC_ACCOUNTS: Each account needs unique lowercase SHA-256 token digests.")
            if any(digest in digests for digest in account_digests):
                raise SystemExit("SYNC_ACCOUNTS: Token digests must identify only one account.")
            account_root = root / account_id
            store = Store(account_root / "collection.sqlite3", account_root / "media")
            stores[account_id] = store
            for digest in account_digests:
                digests[digest] = store
        self.stores = MappingProxyType(stores)
        self.digests = MappingProxyType(digests)

    def authorize(self, authorization):
        if not isinstance(authorization, str) or not authorization.startswith("Bearer "):
            return None
        token = authorization[7:]
        if not token or "\n" in token or "\r" in token:
            return None
        supplied = hashlib.sha256(token.encode()).hexdigest()
        for digest, store in self.digests.items():
            if hmac.compare_digest(supplied, digest):
                return store
        return None


def valid_uuid(value):
    if not isinstance(value, str) or len(value) != 36:
        return False
    try:
        return UUID(value).int != 0 and str(UUID(value)) == value
    except ValueError:
        return False


def instant(value):
    if not isinstance(value, str) or len(value) > 40 or not re.search(r"(?:Z|[+-][0-9]{2}:[0-9]{2})$", value):
        raise ProtocolError("INVALID_EVENT", "Timestamps must include a UTC offset.")
    try:
        result = datetime.fromisoformat(value.replace("Z", "+00:00"))
        if result.tzinfo is None:
            raise ValueError()
        return result.astimezone(timezone.utc)
    except ValueError:
        raise ProtocolError("INVALID_EVENT", "A timestamp is invalid.") from None


def text(record, key, maximum, required=False):
    value = record.get(key)
    try:
        valid = isinstance(value, str) and len(value.encode("utf-16-le")) <= maximum * 2
    except UnicodeError:
        valid = False
    if not valid or "\0" in value or (required and not value.strip()):
        raise ProtocolError("INVALID_EVENT", "A text field is missing, invalid, or exceeds its limit.")
    return value


def number(record, key, low, high, integral=False):
    value = record.get(key)
    if type(value) not in (int, float) or not low <= value <= high or not math.isfinite(value) or (integral and int(value) != value):
        raise ProtocolError("INVALID_EVENT", "A numeric field is missing or outside its range.")
    return value


def identifiers(record, *keys):
    if not isinstance(record, dict) or any(not valid_uuid(record.get(key)) for key in keys):
        raise ProtocolError("INVALID_EVENT", "A record identifier is invalid.")


def card_keys(card):
    identifiers(card, "id", "deckId")
    kind = text(card, "kind", 16, True)
    front = text(card, "front", 1024 * 1024, True)
    back = text(card, "back", 1024 * 1024)
    text(card, "tags", 4096)
    number(card, "pointCount", 1, 1000, True)
    narrations = card.get("narrations", {})
    if not isinstance(narrations, dict):
        raise ProtocolError("INVALID_EVENT", "Card narrations must be an object.")
    fingerprints = card.get("narrationFingerprints", {})
    if not isinstance(fingerprints, dict):
        raise ProtocolError("INVALID_EVENT", "Card narration fingerprints must be an object.")
    for key, value in fingerprints.items():
        if key not in narrations or not isinstance(value, str) or not re.fullmatch(r"[0-9a-f]{64}", value):
            raise ProtocolError("INVALID_EVENT", "Card narration fingerprints must reference an attached narration and contain a SHA-256 digest.")
    def validate_narrations(keys):
        for key, value in narrations.items():
            parts = key.split("/")
            if (len(parts) != 2 or parts[0] not in keys or parts[1] not in ("front", "back")
                    or not isinstance(value, str) or not re.fullmatch(r"[0-9a-f]{64}\.wav", value)):
                raise ProtocolError("INVALID_EVENT", "Card narration references must target a valid variant side and WAV asset.")
    if kind in ("basic", "reverse") and back.strip():
        keys = ["forward"] if kind == "basic" else ["forward", "reverse"]
        validate_narrations(keys)
        return keys
    if kind != "cloze":
        raise ProtocolError("INVALID_EVENT", "Cards must have a supported kind and the required sides.")
    groups, offset = set(), 0
    while offset < len(front):
        opening, stray = front.find("{{", offset), front.find("}}", offset)
        if stray >= 0 and (opening < 0 or stray < opening):
            raise ProtocolError("INVALID_EVENT", "A cloze marker is unmatched.")
        if opening < 0:
            break
        closing, nested = front.find("}}", opening + 2), front.find("{{", opening + 2)
        body = front[opening + 2:closing] if closing >= 0 else ""
        match = re.match(r"c([1-9][0-9]{0,2})::", body)
        if not match or (nested >= 0 and nested < closing):
            raise ProtocolError("INVALID_EVENT", "Cloze markers must have a group from 1 through 999 and a nonempty answer.")
        remaining = body[match.end():]
        separator, search = -1, 0
        while (candidate := remaining.find("::", search)) >= 0:
            slash, position = 0, candidate - 1
            while position >= 0 and remaining[position] == "\\":
                slash += 1
                position -= 1
            if slash % 2 == 0:
                separator = candidate
                break
            search = candidate + 2
        answer = remaining if separator < 0 else remaining[:separator]
        if not answer.strip() or (separator >= 0 and not remaining[separator + 2:].strip()):
            raise ProtocolError("INVALID_EVENT", "A cloze answer and its optional hint must be nonempty.")
        groups.add(int(match[1]))
        offset = closing + 2
    if not groups:
        raise ProtocolError("INVALID_EVENT", "A cloze card needs at least one hidden answer.")
    keys = [f"c{group}" for group in sorted(groups)]
    validate_narrations(keys)
    return keys


def validate_variant(record):
    identifiers(record, "id", "cardId")
    key = text(record, "key", 16, True)
    if key not in ("forward", "reverse") and not re.fullmatch(r"c[1-9][0-9]{0,2}", key):
        raise ProtocolError("INVALID_EVENT", "A review variant key is invalid.")
    if record["id"] != str(uuid5(UUID(record["cardId"]), key)):
        raise ProtocolError("INVALID_EVENT", "A review variant identifier does not match its card and key.")
    instant(text(record, "due", 40, True))
    number(record, "reviewCount", 0, 1000000000, True)
    number(record, "stability", 0.25, 3650)
    number(record, "difficulty", 0, 1)


def review_session(record):
    fields = ("sessionId", "sessionDeckName", "sessionStartedAt")
    present = sum(key in record for key in fields)
    if present == 0:
        return None
    if present != len(fields):
        raise ProtocolError("INVALID_EVENT", "Review session metadata must be supplied as a complete set.")
    identifiers(record, "sessionId")
    return (record["sessionId"], text(record, "sessionDeckName", 256, True),
            instant(text(record, "sessionStartedAt", 40, True)))


def validate_review(record):
    identifiers(record, "id", "cardId", "variantId")
    text(record, "deckName", 256, True)
    number(record, "grade", 0, 4, True)
    number(record, "recallFraction", 0, 1)
    number(record, "responseSeconds", 0, 86400)
    instant(text(record, "reviewedAt", 40, True))
    instant(text(record, "due", 40, True))
    review_session(record)


def validate_deck(record):
    identifiers(record, "id")
    text(record, "name", 256, True)
    text(record, "description", 65536)
    instant(text(record, "createdAt", 40, True))
    parent_id = record.get("parentId", "")
    if not isinstance(parent_id, str) or (parent_id and not valid_uuid(parent_id)) or parent_id == record["id"]:
        raise ProtocolError("INVALID_EVENT", "A deck parent identifier is invalid.")


def validate_history_record(entity, identifier, record):
    if record is None:
        return
    if not isinstance(record, dict):
        raise ProtocolError("INVALID_EVENT", "History states must be objects or explicit null values.")
    if record.get("id") != identifier:
        raise ProtocolError("INVALID_EVENT", "History state identifiers must match their change identifier.")
    match entity:
        case "deck":
            validate_deck(record)
        case "card":
            card_keys(record)
        case "variant":
            validate_variant(record)
        case "review":
            validate_review(record)
        case "owner":
            if set(record) != {"id", "reviewId"}:
                raise ProtocolError("INVALID_EVENT", "History schedule ownership must contain its variant and review identifiers.")
            identifiers(record, "id", "reviewId")
        case _:
            raise ProtocolError("INVALID_EVENT", "History changes contain an unsupported entity.")


def validate_history(payload):
    changes = payload.get("changes")
    if not isinstance(changes, list) or not 1 <= len(changes) <= 100000:
        raise ProtocolError("INVALID_EVENT", "History events must contain between 1 and 100000 changes.")
    seen, context_variants, context_owners = set(), set(), set()
    for change in changes:
        if not isinstance(change, dict):
            raise ProtocolError("INVALID_EVENT", "Each history change must be an object.")
        entity, identifier = change.get("entity"), change.get("id")
        if (entity not in ("deck", "card", "variant", "review", "owner") or not valid_uuid(identifier)
                or "before" not in change or "after" not in change):
            raise ProtocolError("INVALID_EVENT", "History changes require an entity, UUID, and explicit before and after states.")
        identity = (entity, identifier)
        if identity in seen:
            raise ProtocolError("INVALID_EVENT", "History changes must identify each entity only once.")
        seen.add(identity)
        before, after = change["before"], change["after"]
        if "contextOnly" in change:
            if (type(change["contextOnly"]) is not bool or not change["contextOnly"]
                    or entity not in ("variant", "owner") or before is not None or after is None):
                raise ProtocolError("INVALID_EVENT", "History context must be an explicit creation context for a variant or its owner.")
            (context_variants if entity == "variant" else context_owners).add(identifier)
        if before == after:
            raise ProtocolError("INVALID_EVENT", "Each history change must change at least one existing state.")
        validate_history_record(entity, identifier, before)
        validate_history_record(entity, identifier, after)
    if not context_owners <= context_variants:
        raise ProtocolError("INVALID_EVENT", "History owner context requires matching variant context in the same event.")


def validate_payload(kind, payload):
    if "resolvedEventId" in payload and (kind != "deck.upsert" or not valid_uuid(payload["resolvedEventId"])):
        raise ProtocolError("INVALID_EVENT", "Hierarchy repairs must identify their source event.")
    if "restoreContainerOnly" in payload and (kind != "deck.upsert" or payload.get("restore") is not True
            or payload["restoreContainerOnly"] is not True or "resolvedEventId" not in payload):
        raise ProtocolError("INVALID_EVENT", "A hierarchy repair can explicitly restore only its deck container.")
    if "restore" in payload and (kind not in ("deck.upsert", "card.upsert") or type(payload["restore"]) is not bool):
        raise ProtocolError("INVALID_EVENT", "Restore intent must be a boolean on a deck or card upsert.")
    if "freshVariantIds" in payload and (kind != "card.upsert" or not payload.get("restore", False) or not isinstance(payload["freshVariantIds"], list)):
        raise ProtocolError("INVALID_EVENT", "Fresh variant identifiers belong only to an explicit card restore.")
    if "restoreDependency" in payload and (kind != "deck.upsert" or not payload.get("restore", False) or type(payload["restoreDependency"]) is not bool):
        raise ProtocolError("INVALID_EVENT", "A restore dependency must be a boolean on an explicit deck restore.")
    if "historyOnly" in payload and (kind not in ("review.add", "review.correct") or type(payload["historyOnly"]) is not bool):
        raise ProtocolError("INVALID_EVENT", "historyOnly must be a boolean on a review event.")
    match kind:
        case "deck.upsert":
            validate_deck(payload.get("deck"))
        case "deck.delete" | "card.delete":
            identifiers(payload, "id")
        case "card.upsert":
            card, variants = payload.get("card"), payload.get("variants")
            expected = card_keys(card)
            if not isinstance(variants, list) or len(variants) != len(expected):
                raise ProtocolError("INVALID_EVENT", "A card must include exactly its review variants.")
            keys = set()
            for variant in variants:
                validate_variant(variant)
                if variant["cardId"] != card["id"] or variant["key"] not in expected or variant["key"] in keys:
                    raise ProtocolError("INVALID_EVENT", "Card variant dependencies are invalid or duplicated.")
                keys.add(variant["key"])
            if payload.get("restore", False):
                fresh = payload.get("freshVariantIds")
                variant_ids = {variant["id"] for variant in variants}
                if (not isinstance(fresh, list) or len(fresh) > len(variants)
                        or any(not isinstance(value, str) or value not in variant_ids for value in fresh)
                        or len(set(fresh)) != len(fresh)):
                    raise ProtocolError("INVALID_EVENT", "Fresh variant identifiers must be unique identifiers from this card's variants.")
        case "review.add" | "review.correct":
            review = payload.get("review")
            validate_review(review)
            if "historyOnly" in payload and type(payload["historyOnly"]) is not bool:
                raise ProtocolError("INVALID_EVENT", "historyOnly must be a boolean.")
            if kind == "review.correct" and payload.get("historyOnly", False):
                raise ProtocolError("INVALID_EVENT", "A review correction must include its schedule.")
            if not payload.get("historyOnly", False):
                variant = payload.get("variant")
                validate_variant(variant)
                if review["cardId"] != variant["cardId"] or review["variantId"] != variant["id"] or instant(review["due"]) != instant(variant["due"]):
                    raise ProtocolError("INVALID_EVENT", "Review and schedule dependencies do not match.")
                if kind == "review.correct":
                    previous_review, previous_variant = payload.get("previousReview"), payload.get("previousVariant")
                    validate_review(previous_review)
                    validate_variant(previous_variant)
                    if (any(review[key] != previous_review[key] for key in ("id", "cardId", "variantId", "deckName", "responseSeconds"))
                            or instant(review["reviewedAt"]) != instant(previous_review["reviewedAt"])
                            or review_session(review) != review_session(previous_review)
                            or any(variant[key] != previous_variant[key] for key in ("id", "cardId", "key", "reviewCount"))
                            or variant["reviewCount"] < 1
                            or instant(previous_review["due"]) != instant(previous_variant["due"])):
                        raise ProtocolError("INVALID_EVENT", "A correction must preserve the review identity, time, response duration, and review count.")
        case "variant.upsert":
            validate_variant(payload.get("variant"))
        case "history.apply":
            validate_history(payload)


def validate_event(event, device):
    if not isinstance(event, dict) or not valid_uuid(event.get("id")):
        raise ProtocolError("INVALID_EVENT", "Events must have a UUID identifier.")
    if (event.get("deviceId") != device or not isinstance(event.get("type"), str)
            or event["type"] not in EVENT_TYPES):
        raise ProtocolError("INVALID_EVENT", "The event's device or operation is invalid.")
    if not isinstance(event.get("payload"), dict):
        raise ProtocolError("INVALID_EVENT", "The event payload must be an object.")
    instant(event.get("createdAt"))
    validate_payload(event["type"], event["payload"])
    try:
        canonical = json.dumps(event, separators=(",", ":"), sort_keys=True, ensure_ascii=False, allow_nan=False)
        size = len(canonical.encode())
    except (ValueError, TypeError, UnicodeError):
        raise ProtocolError("INVALID_EVENT", "Event values must be valid JSON.") from None
    if size > EVENT_BYTES:
        raise ProtocolError("INVALID_EVENT", "The event exceeds the request limit.")
    return canonical


def card_media(card):
    image_names = set(MEDIA_REFERENCE.findall(card["front"]) + MEDIA_REFERENCE.findall(card["back"]))
    if any(not re.fullmatch(r"[0-9a-f]{64}\.(?:png|jpg|jpeg|webp|gif)", name) for name in image_names):
        raise ProtocolError("MEDIA_REFERENCE", "Attach images using valid collection media filenames.")
    names = set(image_names)
    narrations = card.get("narrations", {})
    if isinstance(narrations, dict):
        names.update(value for value in narrations.values() if isinstance(value, str))
    if any(not re.fullmatch(r"[0-9a-f]{64}\.(?:png|jpg|jpeg|webp|gif|wav)", name) for name in names):
        raise ProtocolError("MEDIA_REFERENCE", "Attach images and narration using valid collection media filenames.")
    return names


def event_media(event):
    match event["type"]:
        case "card.upsert":
            return card_media(event["payload"]["card"])
        case "history.apply":
            names = set()
            for change in event["payload"]["changes"]:
                if change["entity"] == "card" and change["after"] is not None:
                    names.update(card_media(change["after"]))
            return names
        case _:
            return set()


def valid_media_bytes(name, data):
    if name.endswith(".wav"):
        if len(data) < 44 or len(data) > 44 + NARRATION_MAX_PCM_BYTES or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
            return False
        u16 = lambda at: int.from_bytes(data[at:at + 2], "little")
        u32 = lambda at: int.from_bytes(data[at:at + 4], "little")
        return (u32(4) == len(data) - 8 and data[12:16] == b"fmt " and u32(16) == 16
                and u16(20) == 1 and u16(22) == NARRATION_CHANNELS and u32(24) == NARRATION_SAMPLE_RATE
                and u32(28) == NARRATION_SAMPLE_RATE * NARRATION_CHANNELS * NARRATION_BYTES_PER_SAMPLE
                and u16(32) == NARRATION_CHANNELS * NARRATION_BYTES_PER_SAMPLE and u16(34) == NARRATION_BYTES_PER_SAMPLE * 8
                and data[36:40] == b"data"
                and u32(40) == len(data) - 44 and u32(40) > 0 and u32(40) % 2 == 0)
    return True


class Store:
    def __init__(self, path, media):
        self.path = str(path)
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        self.media = Path(media).resolve()
        self.media.mkdir(parents=True, exist_ok=True)
        with closing(self.connect()) as database:
            database.execute("PRAGMA journal_mode=WAL")
            database.execute("CREATE TABLE IF NOT EXISTS events("
                             "seq INTEGER PRIMARY KEY AUTOINCREMENT,"
                             "event_id TEXT UNIQUE NOT NULL,device_id TEXT NOT NULL,"
                             "event_json TEXT NOT NULL,created_at TEXT NOT NULL)")
            database.execute("CREATE TABLE IF NOT EXISTS content_heads("
                             "entity TEXT NOT NULL,entity_id TEXT NOT NULL,seq INTEGER NOT NULL,"
                             "event_id TEXT NOT NULL,device_id TEXT NOT NULL,content_json TEXT,"
                             "PRIMARY KEY(entity,entity_id))")
            database.execute("CREATE TABLE IF NOT EXISTS deck_parents("
                             "deck_id TEXT PRIMARY KEY,parent_id TEXT NOT NULL,deleted INTEGER NOT NULL)")
            database.execute("CREATE TABLE IF NOT EXISTS hierarchy_resolutions("
                             "original_event_id TEXT NOT NULL,resolution_seq INTEGER UNIQUE NOT NULL)")
            database.execute("CREATE TABLE IF NOT EXISTS content_conflicts("
                             "seq INTEGER NOT NULL,entity TEXT NOT NULL,entity_id TEXT NOT NULL,"
                             "previous_seq INTEGER NOT NULL,previous_event_id TEXT NOT NULL,event_id TEXT NOT NULL)")
            database.execute("CREATE INDEX IF NOT EXISTS content_conflicts_seq ON content_conflicts(seq)")
            database.execute("CREATE UNIQUE INDEX IF NOT EXISTS content_conflicts_entity_event "
                             "ON content_conflicts(seq,entity,entity_id)")
            database.execute("BEGIN IMMEDIATE")
            database.execute("CREATE TABLE IF NOT EXISTS server_metadata(collection_id TEXT NOT NULL)")
            metadata_rows = database.execute("SELECT collection_id FROM server_metadata").fetchall()
            if len(metadata_rows) > 1:
                raise ProtocolError("STORAGE_ERROR", "The server collection metadata is not unique.", 500)
            if not metadata_rows:
                collection_id = str(uuid4())
                database.execute("INSERT INTO server_metadata(collection_id) VALUES(?)", (collection_id,))
            else:
                collection_id = metadata_rows[0][0]
                if not valid_uuid(collection_id):
                    raise ProtocolError("STORAGE_ERROR", "The server collection identifier is invalid.", 500)
            self.collection_id = collection_id
            try:
                projected = database.execute("SELECT 1 FROM sqlite_master WHERE type='table' AND name='review_sessions'").fetchone()
                database.execute("CREATE TABLE IF NOT EXISTS review_sessions("
                                 "session_id TEXT PRIMARY KEY,deck_name TEXT NOT NULL,started_at TEXT NOT NULL)")
                if not projected:
                    for row in database.execute("SELECT event_json FROM events ORDER BY seq"):
                        self.record_review_session(database, json.loads(row["event_json"]))
                if database.execute("SELECT 1 FROM content_heads LIMIT 1").fetchone() is None:
                    for row in database.execute("SELECT seq,event_id,device_id,event_json FROM events ORDER BY seq"):
                        for entity, entity_id, content in self.content_targets(json.loads(row["event_json"])):
                            database.execute("INSERT OR REPLACE INTO content_heads(entity,entity_id,seq,event_id,device_id,content_json) VALUES(?,?,?,?,?,?)",
                                             (entity, entity_id, row["seq"], row["event_id"], row["device_id"], content))
                if database.execute("SELECT 1 FROM deck_parents LIMIT 1").fetchone() is None:
                    pending_repairs = {}
                    with closing(database.execute("SELECT seq,event_id,event_json FROM events ORDER BY seq")) as legacy_rows:
                        for row in legacy_rows:
                            event = json.loads(row["event_json"])
                            corrections = self.project_deck(database, event)
                            affected = set()
                            if event["type"] == "deck.upsert":
                                affected.add(event["payload"]["deck"]["id"])
                            elif event["type"] == "deck.delete":
                                affected.add(event["payload"]["id"])
                            elif event["type"] == "history.apply":
                                affected.update(change["id"] for change in event["payload"]["changes"] if change["entity"] == "deck")
                            for deck_id in affected:
                                pending_repairs.pop(deck_id, None)
                            for corrected, container_restore in corrections or ():
                                pending_repairs[corrected["id"]] = (event, corrected, container_restore)
                    for event, corrected, container_restore in pending_repairs.values():
                        final = database.execute("SELECT deleted FROM deck_parents WHERE deck_id=?", (corrected["id"],)).fetchone()
                        if final is not None and not final["deleted"]:
                            self.append_repairs(database, event, [(corrected, container_restore)])
                database.commit()
            except BaseException:
                database.rollback()
                raise

    def connect(self):
        database = sqlite3.connect(self.path, timeout=15, isolation_level=None)
        database.row_factory = sqlite3.Row
        database.execute("PRAGMA synchronous=FULL")
        return database

    @staticmethod
    def deck_would_cycle(database, deck_id, parent_id):
        seen = set()
        current = parent_id
        while current:
            if current == deck_id:
                return True
            if current in seen:
                return True
            seen.add(current)
            row = database.execute("SELECT parent_id,deleted FROM deck_parents WHERE deck_id=?", (current,)).fetchone()
            if row is None or row["deleted"]:
                return False
            current = row["parent_id"]
        return False

    @staticmethod
    def mark_deleted(database, deck_id):
        pending = [deck_id]
        while pending:
            current = pending.pop()
            children = database.execute("SELECT deck_id FROM deck_parents WHERE parent_id=? AND deleted=0", (current,)).fetchall()
            database.execute("INSERT INTO deck_parents(deck_id,parent_id,deleted) VALUES(?,?,1) "
                             "ON CONFLICT(deck_id) DO UPDATE SET parent_id='',deleted=1", (current, ""))
            pending.extend(child[0] for child in children)

    @staticmethod
    def project_deck(database, event):
        kind = event["type"]
        if kind == "deck.delete":
            Store.mark_deleted(database, event["payload"]["id"])
            return []
        if kind == "history.apply":
            changes = {change["id"]: change for change in event["payload"]["changes"]
                       if change["entity"] == "deck" and not change.get("contextOnly")}
            previous = {deck_id: database.execute(
                "SELECT parent_id,deleted FROM deck_parents WHERE deck_id=?", (deck_id,)).fetchone()
                for deck_id in changes}
            # One history event supplies an atomic final hierarchy.
            for deck_id, change in changes.items():
                after = change["after"]
                if after is None:
                    continue
                parent = after.get("parentId", "")
                database.execute("INSERT INTO deck_parents(deck_id,parent_id,deleted) VALUES(?,?,?) "
                                 "ON CONFLICT(deck_id) DO UPDATE SET parent_id=excluded.parent_id,deleted=excluded.deleted",
                                 (deck_id, parent, int(after is None)))
            for deck_id, change in changes.items():
                if change["after"] is None:
                    Store.mark_deleted(database, deck_id)
            corrections = []
            for deck_id in sorted(changes, reverse=True):
                after = changes[deck_id]["after"]
                current = database.execute("SELECT parent_id,deleted FROM deck_parents WHERE deck_id=?", (deck_id,)).fetchone()
                if (after is None or current["deleted"]
                        or not Store.deck_would_cycle(database, deck_id, current["parent_id"])):
                    continue
                old = previous[deck_id]
                parent = old["parent_id"] if old is not None and not old["deleted"] else ""
                database.execute("UPDATE deck_parents SET parent_id='' WHERE deck_id=?", (deck_id,))
                if Store.deck_would_cycle(database, deck_id, parent):
                    parent = ""
                database.execute("UPDATE deck_parents SET parent_id=? WHERE deck_id=?", (parent, deck_id))
                corrected = dict(after)
                corrected["parentId"] = parent
                corrections.append((corrected, True))
            return corrections
        if kind != "deck.upsert":
            return None
        deck = event["payload"]["deck"]
        existing = database.execute("SELECT parent_id,deleted FROM deck_parents WHERE deck_id=?", (deck["id"],)).fetchone()
        proposed = deck.get("parentId", "")
        if existing is not None and existing["deleted"] and not event["payload"].get("restore", False):
            return []
        if event["payload"].get("restoreDependency") and existing is not None and not existing["deleted"]:
            proposed = existing["parent_id"]
        parent = database.execute("SELECT deleted FROM deck_parents WHERE deck_id=?", (proposed,)).fetchone() if proposed else None
        if parent is not None and parent["deleted"] and not event["payload"].get("restore", False):
            corrected = dict(deck)
            corrected["parentId"] = existing["parent_id"] if existing is not None else ""
            return [(corrected, True)]
        if not Store.deck_would_cycle(database, deck["id"], proposed):
            database.execute("INSERT INTO deck_parents(deck_id,parent_id,deleted) VALUES(?,?,0) "
                             "ON CONFLICT(deck_id) DO UPDATE SET parent_id=excluded.parent_id,deleted=0",
                             (deck["id"], proposed))
            return []
        previous = existing["parent_id"] if existing is not None and not existing["deleted"] else ""
        corrected = dict(deck)
        corrected["parentId"] = previous
        database.execute("INSERT INTO deck_parents(deck_id,parent_id,deleted) VALUES(?,?,0) "
                         "ON CONFLICT(deck_id) DO UPDATE SET parent_id=excluded.parent_id,deleted=0",
                         (deck["id"], previous))
        return [(corrected, False)]

    def append_repairs(self, database, source_event, corrections):
        for corrected, container_restore in corrections:
            generated = {"id": str(uuid4()), "deviceId": self.collection_id,
                         "type": "deck.upsert", "createdAt": datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z"),
                         "payload": {"deck": corrected, "resolvedEventId": source_event["id"]}}
            if source_event.get("payload", {}).get("restore") is True:
                generated["payload"]["restore"] = True
            if container_restore or source_event["payload"].get("restoreContainerOnly") is True:
                generated["payload"]["restore"] = True
                generated["payload"]["restoreContainerOnly"] = True
            generated_canonical = validate_event(generated, self.collection_id)
            generated_result = database.execute(
                "INSERT INTO events(event_id,device_id,event_json,created_at) VALUES(?,?,?,?)",
                (generated["id"], generated["deviceId"], generated_canonical, generated["createdAt"]))
            generated_seq = generated_result.lastrowid
            database.execute("INSERT INTO hierarchy_resolutions(original_event_id,resolution_seq) VALUES(?,?)",
                             (source_event["id"], generated_seq))
            database.execute("INSERT OR REPLACE INTO content_heads(entity,entity_id,seq,event_id,device_id,content_json) VALUES(?,?,?,?,?,?)",
                             ("deck", corrected["id"], generated_seq, generated["id"], generated["deviceId"],
                              json.dumps(corrected, separators=(",", ":"), sort_keys=True, ensure_ascii=False)))
            self.project_deck(database, generated)

    @staticmethod
    def content_targets(event):
        kind, payload = event["type"], event["payload"]
        if kind == "deck.upsert" and not payload.get("restoreDependency", False):
            record = payload["deck"]
            return [("deck", record["id"], json.dumps(record, separators=(",", ":"), sort_keys=True, ensure_ascii=False))]
        if kind == "card.upsert":
            record = payload["card"]
            return [("card", record["id"], json.dumps(record, separators=(",", ":"), sort_keys=True, ensure_ascii=False))]
        if kind in ("deck.delete", "card.delete"):
            return [(kind.split(".")[0], payload["id"], None)]
        if kind == "history.apply":
            result = []
            for change in payload["changes"]:
                if change.get("contextOnly") or change["entity"] not in ("deck", "card"):
                    continue
                after = change["after"]
                content = None if after is None else json.dumps(after, separators=(",", ":"), sort_keys=True, ensure_ascii=False)
                result.append((change["entity"], change["id"], content))
            return result
        return []

    @staticmethod
    def record_review_session(database, event):
        match event["type"]:
            case "review.add" | "review.correct":
                reviews = [event["payload"]["review"]]
            case "history.apply":
                reviews = [change[side] for change in event["payload"]["changes"]
                           if change["entity"] == "review" for side in ("before", "after")
                           if change[side] is not None]
            case _:
                return
        for review in reviews:
            session = review_session(review)
            if session is None:
                continue
            session_id, deck_name, started = session
            started_at = started.isoformat(timespec="milliseconds")
            existing = database.execute("SELECT deck_name,started_at FROM review_sessions WHERE session_id=?",
                                        (session_id,)).fetchone()
            if existing and (existing["deck_name"] != deck_name or existing["started_at"] != started_at):
                raise ProtocolError("SESSION_CONFLICT", "This session identifier already has a different deck or start time. Refresh the originating client before syncing again.", 409)
            if not existing:
                database.execute("INSERT INTO review_sessions(session_id,deck_name,started_at) VALUES(?,?,?)",
                                 (session_id, deck_name, started_at))

    def media_sizes(self, event):
        result = {}
        for name in event_media(event):
            try:
                metadata = (self.media / name).lstat()
            except OSError:
                raise ProtocolError("MEDIA_MISSING", "Upload the referenced image before sending its card.", 422) from None
            if not stat.S_ISREG(metadata.st_mode) or not 0 < metadata.st_size <= MEDIA_LIMIT:
                raise ProtocolError("MEDIA_STORAGE", "A referenced image is missing or invalid. Restore the server media file.", 422)
            result[name] = metadata.st_size
        if len(result) > PAGE_MEDIA_COUNT or sum(result.values()) > PAGE_MEDIA_LIMIT:
            raise ProtocolError("MEDIA_EVENT_LIMIT", "A card can reference at most 64 images and 64 MiB of attached data.", 413)
        return result

    def sync(self, body):
        if not isinstance(body, dict) or not valid_uuid(body.get("deviceId")):
            raise ProtocolError("INVALID_REQUEST", "Send a device UUID, cursor, and event array.")
        cursor, events = body.get("cursor"), body.get("events")
        if type(cursor) is not int or not isinstance(events, list):
            raise ProtocolError("INVALID_REQUEST", "The cursor must be an integer and events an array.")
        collection_id = body.get("collectionId")
        if collection_id is not None and not valid_uuid(collection_id):
            raise ProtocolError("INVALID_REQUEST", "The collection identifier is invalid.")
        if collection_id is not None and collection_id != self.collection_id:
            raise ProtocolError("COLLECTION_MISMATCH", "This collection belongs to a different sync account.", 409)
        if len(events) > EVENT_LIMIT:
            raise ProtocolError("EVENT_LIMIT", "Send at most 100 events per batch.")
        canonical_events = [(event, validate_event(event, body["deviceId"])) for event in events]
        with closing(self.connect()) as database:
            database.execute("BEGIN IMMEDIATE")
            try:
                latest = database.execute("SELECT COALESCE(MAX(seq),0) FROM events").fetchone()[0]
                if cursor < 0 or cursor > latest:
                    raise ProtocolError("INVALID_CURSOR", "The cursor is outside this server's event log.")
                accepted = []
                batch_event_ids = {event[0]["id"] for event in canonical_events}
                for event, canonical in canonical_events:
                    existing = database.execute("SELECT event_json FROM events WHERE event_id=?", (event["id"],)).fetchone()
                    if existing and existing[0] != canonical:
                        raise Conflict()
                    if not existing:
                        self.media_sizes(event)
                        self.record_review_session(database, event)
                        cursor_result = database.execute("INSERT INTO events(event_id,device_id,event_json,created_at) VALUES(?,?,?,?)",
                                         (event["id"], body["deviceId"], canonical, event["createdAt"]))
                        event_seq = cursor_result.lastrowid
                        for entity, entity_id, content in self.content_targets(event):
                            head = database.execute("SELECT seq,event_id,device_id,content_json FROM content_heads WHERE entity=? AND entity_id=?",
                                                    (entity, entity_id)).fetchone()
                            if (head is not None and head["seq"] > cursor and head["event_id"] not in batch_event_ids
                                    and head["content_json"] != content):
                                database.execute("INSERT INTO content_conflicts(seq,entity,entity_id,previous_seq,previous_event_id,event_id) VALUES(?,?,?,?,?,?)",
                                                 (event_seq, entity, entity_id, head["seq"], head["event_id"], event["id"]))
                            database.execute("INSERT OR REPLACE INTO content_heads(entity,entity_id,seq,event_id,device_id,content_json) VALUES(?,?,?,?,?,?)",
                                             (entity, entity_id, event_seq, event["id"], body["deviceId"], content))
                        corrections = self.project_deck(database, event)
                        if corrections:
                            self.append_repairs(database, event, corrections)
                    if event["id"] not in accepted:
                        accepted.append(event["id"])
                outgoing, response_bytes, media_sizes = [], 0, {}
                with closing(database.execute("SELECT seq,event_json FROM events WHERE seq>? ORDER BY seq LIMIT ?",
                                              (cursor, RESPONSE_LIMIT))) as rows:
                    for row in rows:
                        size = len(row["event_json"].encode()) + 64
                        if outgoing and response_bytes + size > RESPONSE_BYTES:
                            break
                        event = json.loads(row["event_json"])
                        assets = {**media_sizes, **self.media_sizes(event)}
                        if outgoing and (len(assets) > PAGE_MEDIA_COUNT or sum(assets.values()) > PAGE_MEDIA_LIMIT):
                            break
                        media_sizes = assets
                        outgoing.append({"seq": row["seq"], "event": event})
                        response_bytes += size
                next_cursor = outgoing[-1]["seq"] if outgoing else cursor
                latest = database.execute("SELECT COALESCE(MAX(seq),0) FROM events").fetchone()[0]
                conflicts = []
                conflict_count = 0
                hierarchy_count = 0
                if outgoing:
                    conflict_count = database.execute(
                        "SELECT COUNT(*) FROM content_conflicts WHERE seq>? AND seq<=?",
                        (cursor, next_cursor)).fetchone()[0]
                    conflicts = [dict(row) for row in database.execute(
                        "SELECT seq,event_id AS eventId,entity,entity_id AS id,previous_seq AS previousSeq,previous_event_id AS previousEventId "
                        "FROM content_conflicts WHERE seq>? AND seq<=? ORDER BY seq LIMIT 1000",
                        (cursor, next_cursor)).fetchall()]
                    hierarchy_count = database.execute(
                        "SELECT COUNT(*) FROM hierarchy_resolutions WHERE resolution_seq>? AND resolution_seq<=?",
                        (cursor, next_cursor)).fetchone()[0]
                response = {"acceptedIds": accepted, "events": outgoing, "cursor": next_cursor,
                            "hasMore": next_cursor < latest,
                            "media": sorted(media_sizes), "collectionId": self.collection_id,
                            "contentConflicts": conflicts, "contentConflictCount": conflict_count,
                            "hierarchyResolutionCount": hierarchy_count}
                database.commit()
                return response
            except BaseException:
                database.rollback()
                raise


def decode_json(data):
    def invalid_constant(_):
        raise ValueError("nonfinite JSON number")
    def unique_keys(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("duplicate JSON key")
            result[key] = value
        return result
    return json.loads(data, parse_constant=invalid_constant, object_pairs_hook=unique_keys)


class Handler(BaseHTTPRequestHandler):
    store = None
    accounts = None

    def setup(self):
        super().setup()
        self.connection.settimeout(20)

    def log_message(self, *_args):
        # HTTP headers and user content never enter the server log.
        pass

    def send_json(self, status, value, *, body=True):
        data = json.dumps(value, separators=(",", ":"), ensure_ascii=False, allow_nan=False).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        if body:
            self.wfile.write(data)

    def send_error_value(self, failure, *, body=True):
        self.send_json(failure.status, {"error": {"code": failure.code, "message": failure.message}}, body=body)

    def authorized(self, *, body=True):
        if self.accounts is not None:
            store = self.accounts.authorize(self.headers.get("Authorization"))
        elif TOKEN:
            supplied = self.headers.get("Authorization", "").encode()
            expected = ("Bearer " + TOKEN).encode()
            store = self.store if hmac.compare_digest(supplied, expected) else None
        else:
            self.send_error_value(ProtocolError("SERVER_NOT_CONFIGURED", "Configure a server access token.", 503), body=body)
            return None
        if store is None:
            self.send_error_value(ProtocolError("UNAUTHORIZED", "Use the server's configured access token.", 401), body=body)
            return None
        return store

    def collection_matches(self, store, *, required, body=True):
        supplied = self.headers.get("X-BetterFlash-Collection")
        if supplied is None and not required:
            return True
        if supplied != store.collection_id:
            self.send_error_value(ProtocolError("COLLECTION_MISMATCH", "This collection belongs to a different sync account.", 409), body=body)
            return False
        return True

    def read_body(self, limit):
        if self.headers.get("Transfer-Encoding"):
            raise ProtocolError("BODY_ENCODING", "Send an explicit Content-Length.")
        try:
            length = int(self.headers.get("Content-Length", "-1"))
        except ValueError:
            length = -1
        if length < 0:
            raise ProtocolError("BODY_LENGTH", "Send an explicit Content-Length.", 411)
        if length > limit:
            raise ProtocolError("BODY_LIMIT", "The request exceeds the size limit.", 413)
        try:
            data = self.rfile.read(length)
        except TimeoutError:
            raise ProtocolError("BODY_TIMEOUT", "The request body did not arrive in time.", 408) from None
        if len(data) != length:
            raise ProtocolError("BODY_INCOMPLETE", "The request body ended early.")
        return data

    def get_media(self, *, body):
        path = urlsplit(self.path).path
        if path == "/health":
            self.send_json(200, {"ok": True}, body=body)
            return
        if path == "/v1/info":
            store = self.authorized(body=body)
            if store is not None:
                self.send_json(200, {"collectionId": store.collection_id, "protocolVersion": 2,
                                     "capabilities": ["independent-schedules", "hierarchy-repair"]}, body=body)
            return
        match = MEDIA_RE.fullmatch(path)
        if not match:
            self.send_error_value(ProtocolError("NOT_FOUND", "The endpoint was not found.", 404), body=body)
            return
        store = self.authorized(body=body)
        if store is None:
            return
        if not self.collection_matches(store, required=False, body=body):
            return
        filename = match.group(1)
        file = store.media / filename
        try:
            descriptor = os.open(file, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
        except OSError:
            self.send_error_value(ProtocolError("MEDIA_NOT_FOUND", "The attached image was not found.", 404), body=body)
            return
        with os.fdopen(descriptor, "rb") as source:
            metadata = os.fstat(source.fileno())
            length = metadata.st_size
            if not stat.S_ISREG(metadata.st_mode) or not 0 < length <= MEDIA_LIMIT:
                self.send_error_value(ProtocolError("MEDIA_SIZE", "The stored image exceeds the size limit.", 413), body=body)
                return
            self.send_response(200)
            self.send_header("Content-Type", self.media_type(filename))
            self.send_header("Content-Length", str(length))
            self.end_headers()
            if body:
                while chunk := source.read(64 * 1024):
                    self.wfile.write(chunk)

    def do_GET(self):
        self.get_media(body=True)

    def do_HEAD(self):
        self.get_media(body=False)

    def do_PUT(self):
        match = MEDIA_RE.fullmatch(urlsplit(self.path).path)
        if not match:
            self.send_error_value(ProtocolError("NOT_FOUND", "The endpoint was not found.", 404))
            return
        store = self.authorized()
        if store is None:
            return
        if not self.collection_matches(store, required=True):
            return
        temporary = None
        try:
            data = self.read_body(MEDIA_LIMIT)
            if (not data or hashlib.sha256(data).hexdigest() != match.group(1).split(".")[0]
                    or not valid_media_bytes(match.group(1), data)):
                raise ProtocolError("MEDIA_INVALID", "The media bytes do not match the filename or supported format.", 422)
            target = store.media / match.group(1)
            if target.is_symlink():
                raise ProtocolError("MEDIA_PATH", "The stored image path is invalid.", 409)
            with tempfile.NamedTemporaryFile(dir=store.media, delete=False) as output:
                temporary = Path(output.name)
                output.write(data)
                output.flush()
                os.fsync(output.fileno())
            os.replace(temporary, target)
            temporary = None
            directory = os.open(store.media, os.O_RDONLY | os.O_DIRECTORY)
            try:
                os.fsync(directory)
            finally:
                os.close(directory)
            self.send_json(201, {"filename": target.name})
        except ProtocolError as failure:
            self.send_error_value(failure)
        except OSError:
            self.send_error_value(ProtocolError("MEDIA_STORAGE", "Cannot save the image. Check server storage.", 500))
        finally:
            if temporary is not None:
                temporary.unlink(missing_ok=True)

    def do_POST(self):
        if urlsplit(self.path).path != "/v1/sync":
            self.send_error_value(ProtocolError("NOT_FOUND", "The endpoint was not found.", 404))
            return
        store = self.authorized()
        if store is None:
            return
        try:
            body = decode_json(self.read_body(BODY_LIMIT))
            if self.headers.get("X-BetterFlash-Protocol") != "2":
                raise ProtocolError("CLIENT_UPGRADE_REQUIRED", "Upgrade every synced client to protocol 2 before syncing this collection.", 426)
            self.send_json(200, store.sync(body))
        except ProtocolError as failure:
            self.send_error_value(failure)
        except (ValueError, UnicodeError, RecursionError):
            self.send_error_value(ProtocolError("INVALID_JSON", "Send a valid JSON sync request."))
        except (sqlite3.Error, OSError):
            self.send_error_value(ProtocolError("STORAGE_ERROR", "Cannot commit this batch. Check server storage.", 500))

    @staticmethod
    def media_type(name):
        return {".png": "image/png", ".jpg": "image/jpeg", ".jpeg": "image/jpeg",
                ".webp": "image/webp", ".gif": "image/gif", ".wav": "audio/wav"}[Path(name).suffix]


class BoundedHTTPServer(ThreadingHTTPServer):
    """Limit request threads so large authenticated uploads cannot exhaust memory."""

    daemon_threads = True

    def __init__(self, *args, max_workers=MAX_WORKERS, **kwargs):
        super().__init__(*args, **kwargs)
        self.worker_slots = threading.BoundedSemaphore(max_workers)

    def process_request(self, request, client_address):
        if not self.worker_slots.acquire(blocking=False):
            try:
                request.settimeout(1)
                data = b'{"error":{"code":"SERVER_BUSY"}}'
                request.sendall(b"HTTP/1.1 503 Service Unavailable\r\n"
                                b"Content-Type: application/json\r\n"
                                b"Content-Length: " + str(len(data)).encode() + b"\r\n"
                                b"Connection: close\r\n\r\n"
                                + data)
            except OSError:
                pass
            finally:
                request.close()
            return
        try:
            super().process_request(request, client_address)
        except BaseException:
            self.worker_slots.release()
            raise

    def process_request_thread(self, request, client_address):
        try:
            super().process_request_thread(request, client_address)
        finally:
            self.worker_slots.release()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--listen", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--database")
    parser.add_argument("--media")
    parser.add_argument("--accounts-file")
    parser.add_argument("--data-dir")
    arguments = parser.parse_args()
    os.umask(0o077)
    account_mode = arguments.accounts_file is not None or arguments.data_dir is not None
    explicit_legacy_storage = arguments.database is not None or arguments.media is not None
    if account_mode and (arguments.accounts_file is None or arguments.data_dir is None):
        raise SystemExit("SYNC_ACCOUNTS: --accounts-file and --data-dir must be supplied together.")
    if account_mode and (explicit_legacy_storage or TOKEN):
        raise SystemExit("SYNC_ACCOUNTS: Account mode cannot be combined with legacy token or storage options.")
    if not account_mode:
        if len(TOKEN) < 24 or "\n" in TOKEN or "\r" in TOKEN:
            raise SystemExit("SYNC_TOKEN: Set BETTERFLASH_SYNC_TOKEN to at least 24 characters.")
        Handler.accounts = None
        Handler.store = Store(arguments.database or "betterflash-sync.sqlite3",
                              arguments.media or "betterflash-media")
    else:
        Handler.store = None
        Handler.accounts = Accounts(arguments.accounts_file, arguments.data_dir)
    server = BoundedHTTPServer((arguments.listen, arguments.port), Handler)
    print(f"BetterFlash sync listening on http://{arguments.listen}:{server.server_port}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
