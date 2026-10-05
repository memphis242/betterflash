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
from urllib.parse import urlsplit
from uuid import UUID, uuid5

TOKEN = os.environ.get("BETTERFLASH_SYNC_TOKEN", "")
EVENT_LIMIT = 100
RESPONSE_LIMIT = 1000
BODY_LIMIT = 4 * 1024 * 1024
EVENT_BYTES = 3 * 1024 * 1024
RESPONSE_BYTES = 6 * 1024 * 1024
MEDIA_LIMIT = 20 * 1024 * 1024
PAGE_MEDIA_LIMIT = 64 * 1024 * 1024
PAGE_MEDIA_COUNT = 64
MEDIA_RE = re.compile(r"^/v1/media/([0-9a-f]{64}\.(?:png|jpg|jpeg|webp|gif))$")
EVENT_TYPES = frozenset(("deck.upsert", "deck.delete", "card.upsert", "card.delete", "review.add", "variant.upsert"))
MEDIA_REFERENCE = re.compile(r'''media:([^\s\)\]>"'`]+)''')


class ProtocolError(Exception):
    def __init__(self, code, message, status=400):
        super().__init__(message)
        self.code, self.message, self.status = code, message, status


class Conflict(ProtocolError):
    def __init__(self):
        super().__init__("EVENT_CONFLICT", "An event identifier already contains different data.", 409)


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
    if kind in ("basic", "reverse") and back.strip():
        return ["forward"] if kind == "basic" else ["forward", "reverse"]
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
    return [f"c{group}" for group in sorted(groups)]


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


def validate_review(record):
    identifiers(record, "id", "cardId", "variantId")
    text(record, "deckName", 256, True)
    number(record, "grade", 0, 4, True)
    number(record, "recallFraction", 0, 1)
    number(record, "responseSeconds", 0, 86400)
    instant(text(record, "reviewedAt", 40, True))
    instant(text(record, "due", 40, True))


def validate_payload(kind, payload):
    match kind:
        case "deck.upsert":
            deck = payload.get("deck")
            identifiers(deck, "id")
            text(deck, "name", 256, True)
            text(deck, "description", 65536)
            instant(text(deck, "createdAt", 40, True))
            parent_id = deck.get("parentId", "")
            if not isinstance(parent_id, str) or (parent_id and not valid_uuid(parent_id)) or parent_id == deck["id"]:
                raise ProtocolError("INVALID_EVENT", "A deck parent identifier is invalid.")
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
        case "review.add":
            review = payload.get("review")
            validate_review(review)
            if "historyOnly" in payload and type(payload["historyOnly"]) is not bool:
                raise ProtocolError("INVALID_EVENT", "historyOnly must be a boolean.")
            if not payload.get("historyOnly", False):
                variant = payload.get("variant")
                validate_variant(variant)
                if review["cardId"] != variant["cardId"] or review["variantId"] != variant["id"] or instant(review["due"]) != instant(variant["due"]):
                    raise ProtocolError("INVALID_EVENT", "Review and schedule dependencies do not match.")
        case "variant.upsert":
            validate_variant(payload.get("variant"))


def validate_event(event, device):
    if not isinstance(event, dict) or not valid_uuid(event.get("id")):
        raise ProtocolError("INVALID_EVENT", "Events must have a UUID identifier.")
    if event.get("deviceId") != device or event.get("type") not in EVENT_TYPES:
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


def event_media(event):
    if event["type"] != "card.upsert":
        return set()
    card = event["payload"]["card"]
    names = set(MEDIA_REFERENCE.findall(card["front"]) + MEDIA_REFERENCE.findall(card["back"]))
    if any(not re.fullmatch(r"[0-9a-f]{64}\.(?:png|jpg|jpeg|webp|gif)", name) for name in names):
        raise ProtocolError("MEDIA_REFERENCE", "Attach images using valid collection media filenames.")
    return names


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

    def connect(self):
        database = sqlite3.connect(self.path, timeout=15, isolation_level=None)
        database.row_factory = sqlite3.Row
        database.execute("PRAGMA synchronous=FULL")
        return database

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
                for event, canonical in canonical_events:
                    existing = database.execute("SELECT event_json FROM events WHERE event_id=?", (event["id"],)).fetchone()
                    if existing and existing[0] != canonical:
                        raise Conflict()
                    if not existing:
                        self.media_sizes(event)
                        database.execute("INSERT INTO events(event_id,device_id,event_json,created_at) VALUES(?,?,?,?)",
                                         (event["id"], body["deviceId"], canonical, event["createdAt"]))
                    if event["id"] not in accepted:
                        accepted.append(event["id"])
                rows = database.execute("SELECT seq,event_json FROM events WHERE seq>? ORDER BY seq LIMIT ?",
                                        (cursor, RESPONSE_LIMIT)).fetchall()
                outgoing, response_bytes, media_sizes = [], 0, {}
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
                response = {"acceptedIds": accepted, "events": outgoing, "cursor": next_cursor,
                            "hasMore": next_cursor < latest,
                            "media": sorted(media_sizes)}
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
        if not TOKEN:
            self.send_error_value(ProtocolError("SERVER_NOT_CONFIGURED", "Configure a server access token.", 503), body=body)
            return False
        supplied = self.headers.get("Authorization", "").encode()
        expected = ("Bearer " + TOKEN).encode()
        if not hmac.compare_digest(supplied, expected):
            self.send_error_value(ProtocolError("UNAUTHORIZED", "Use the server's configured access token.", 401), body=body)
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
        match = MEDIA_RE.fullmatch(path)
        if not match:
            self.send_error_value(ProtocolError("NOT_FOUND", "The endpoint was not found.", 404), body=body)
            return
        if not self.authorized(body=body):
            return
        filename = match.group(1)
        file = self.store.media / filename
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
        if not self.authorized():
            return
        temporary = None
        try:
            data = self.read_body(MEDIA_LIMIT)
            if not data or hashlib.sha256(data).hexdigest() != match.group(1).split(".")[0]:
                raise ProtocolError("MEDIA_HASH_MISMATCH", "The image bytes do not match the filename hash.", 422)
            target = self.store.media / match.group(1)
            if target.is_symlink():
                raise ProtocolError("MEDIA_PATH", "The stored image path is invalid.", 409)
            with tempfile.NamedTemporaryFile(dir=self.store.media, delete=False) as output:
                temporary = Path(output.name)
                output.write(data)
                output.flush()
                os.fsync(output.fileno())
            os.replace(temporary, target)
            temporary = None
            directory = os.open(self.store.media, os.O_RDONLY | os.O_DIRECTORY)
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
        if not self.authorized():
            return
        try:
            body = decode_json(self.read_body(BODY_LIMIT))
            self.send_json(200, self.store.sync(body))
        except ProtocolError as failure:
            self.send_error_value(failure)
        except (ValueError, UnicodeError):
            self.send_error_value(ProtocolError("INVALID_JSON", "Send a valid JSON sync request."))
        except sqlite3.Error:
            self.send_error_value(ProtocolError("STORAGE_ERROR", "Cannot commit this batch. Check server storage.", 500))

    @staticmethod
    def media_type(name):
        return {".png": "image/png", ".jpg": "image/jpeg", ".jpeg": "image/jpeg",
                ".webp": "image/webp", ".gif": "image/gif"}[Path(name).suffix]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--listen", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--database", default="betterflash-sync.sqlite3")
    parser.add_argument("--media", default="betterflash-media")
    arguments = parser.parse_args()
    if len(TOKEN) < 24 or "\n" in TOKEN or "\r" in TOKEN:
        raise SystemExit("SYNC_TOKEN: Set BETTERFLASH_SYNC_TOKEN to at least 24 characters.")
    Handler.store = Store(arguments.database, arguments.media)
    server = ThreadingHTTPServer((arguments.listen, arguments.port), Handler)
    print(f"BetterFlash sync listening on http://{arguments.listen}:{server.server_port}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
