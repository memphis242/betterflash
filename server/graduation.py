"""Project review phases and preserve graduation across concurrent reviews."""

import json
from uuid import uuid4


class GraduationProjection:
    def __init__(self, database, collection_id, validate_event, instant, protocol_error, replay=False):
        self.database = database
        self.collection_id = collection_id
        self.validate_event = validate_event
        self.instant = instant
        self.protocol_error = protocol_error
        self.replay = replay
        self.deleted_cards = set()
        self.deleted_decks = set()
        self.known_cards = set()

    def initialize(self):
        self.database.execute("CREATE TABLE IF NOT EXISTS graduation_heads("
                              "variant_id TEXT PRIMARY KEY,card_id TEXT NOT NULL,variant_json TEXT NOT NULL,"
                              "owner_id TEXT NOT NULL,seq INTEGER NOT NULL,event_id TEXT NOT NULL)")
        self.database.execute("CREATE INDEX IF NOT EXISTS graduation_heads_card ON graduation_heads(card_id)")
        self.database.execute("CREATE TABLE IF NOT EXISTS graduation_resolutions("
                              "original_event_id TEXT PRIMARY KEY,resolution_seq INTEGER UNIQUE NOT NULL)")

    def head(self, identifier):
        row = self.database.execute("SELECT variant_json,owner_id,seq,event_id FROM graduation_heads WHERE variant_id=?",
                                    (identifier,)).fetchone()
        if row is None:
            return None
        return {"variant": json.loads(row[0]), "owner": row[1], "seq": row[2], "event_id": row[3]}

    @staticmethod
    def is_graduated(variant):
        return variant.get("graduated", variant["reviewCount"] > 0)

    def same_variant(self, first, second):
        def normalize(value):
            result = {key: value[key] for key in ("id", "cardId", "key", "due", "reviewCount", "stability", "difficulty")}
            result["graduated"] = self.is_graduated(value)
            result["due"] = self.instant(result["due"]).isoformat(timespec="milliseconds")
            return result
        return normalize(first) == normalize(second)

    def store(self, variant, owner, seq, event_id):
        encoded = json.dumps(variant, sort_keys=True, separators=(",", ":"), ensure_ascii=False)
        self.database.execute("INSERT OR REPLACE INTO graduation_heads VALUES(?,?,?,?,?,?)",
                              (variant["id"], variant["cardId"], encoded, owner, seq, event_id))

    def deleted(self, card_id):
        if self.replay:
            return card_id in self.deleted_cards
        row = self.database.execute("SELECT deleted FROM card_locations WHERE card_id=?", (card_id,)).fetchone()
        return row is not None and bool(row[0])

    def known_card(self, card_id):
        if self.replay:
            return card_id in self.known_cards
        row = self.database.execute("SELECT deleted FROM card_locations WHERE card_id=?", (card_id,)).fetchone()
        return row is not None and not row[0]

    def delete_cards(self, card_ids, barriers):
        for card_id in card_ids:
            for row in self.database.execute("SELECT variant_id FROM graduation_heads WHERE card_id=?", (card_id,)):
                barriers.add(row[0])
            self.database.execute("DELETE FROM graduation_heads WHERE card_id=?", (card_id,))
            if self.replay:
                self.deleted_cards.add(card_id)
                self.known_cards.discard(card_id)

    def consume(self, event, seq):
        """Apply an ordered event; return a repair and explicit barrier identifiers."""
        kind, payload = event["type"], event["payload"]
        barriers = set()
        if kind == "card.delete":
            self.delete_cards((payload["id"],), barriers)
        if kind in ("deck.delete", "history.apply"):
            scope = self.database.execute("SELECT scope_json FROM deletion_scopes WHERE seq=?", (seq,)).fetchone()
            if scope is not None:
                scopes = json.loads(scope[0]).values()
                cards = {card_id for value in scopes for card_id in value["cards"]}
                self.delete_cards(cards, barriers)
                if self.replay:
                    self.deleted_decks.update(deck_id for value in scopes for deck_id in value["decks"])
        if self.replay and kind == "deck.upsert" and payload.get("restore", False):
            self.deleted_decks.discard(payload["deck"]["id"])
        if kind == "history.apply":
            if self.replay:
                for change in payload["changes"]:
                    if change["entity"] == "deck" and change["after"] is not None:
                        self.deleted_decks.discard(change["id"])
            for change in payload["changes"]:
                if change["entity"] != "card" or change.get("contextOnly"):
                    continue
                if (change["after"] is None or (self.replay
                        and change["after"]["deckId"] in self.deleted_decks)):
                    self.delete_cards((change["id"],), barriers)
                elif self.replay:
                    self.deleted_cards.discard(change["id"])
                    self.known_cards.add(change["id"])
        if kind == "card.upsert":
            card_id = payload["card"]["id"]
            if self.replay and payload.get("restore", False):
                self.deleted_cards.discard(card_id)
            elif self.replay and payload["card"]["deckId"] in self.deleted_decks:
                self.delete_cards((card_id,), barriers)
                return None, barriers
            if self.deleted(card_id):
                self.delete_cards((card_id,), barriers)
                return None, barriers
            if self.replay:
                self.known_cards.add(card_id)
            variants = {variant["id"]: variant for variant in payload["variants"]}
            existing = self.database.execute("SELECT variant_id FROM graduation_heads WHERE card_id=?", (card_id,)).fetchall()
            for row in existing:
                if row[0] not in variants:
                    self.database.execute("DELETE FROM graduation_heads WHERE variant_id=?", (row[0],))
                    barriers.add(row[0])
            fresh = set(payload.get("freshVariantIds", []))
            for identifier, variant in variants.items():
                if identifier in fresh or self.head(identifier) is None:
                    self.store(variant, "", seq, event["id"])
                    barriers.add(identifier)
            return None, barriers
        if kind == "history.apply":
            for change in payload["changes"]:
                if change["entity"] != "variant":
                    continue
                identifier, after = change["id"], change["after"]
                if change.get("contextOnly") and self.head(identifier) is not None:
                    continue
                if after is not None and not self.known_card(after["cardId"]):
                    continue
                if after is None:
                    self.database.execute("DELETE FROM graduation_heads WHERE variant_id=?", (identifier,))
                elif not self.deleted(after["cardId"]):
                    self.store(after, "", seq, event["id"])
                if not change.get("contextOnly"):
                    barriers.add(identifier)
            for change in payload["changes"]:
                if change["entity"] != "owner":
                    continue
                head = self.head(change["id"])
                if head is None or (change.get("contextOnly") and head["owner"]):
                    continue
                after = change["after"]
                self.store(head["variant"], after["reviewId"] if after else "", head["seq"], head["event_id"])
            return None, barriers
        if kind not in ("review.add", "review.correct", "variant.upsert") or payload.get("historyOnly", False):
            return None, barriers
        variant = payload["variant"]
        if self.deleted(variant["cardId"]):
            return None, barriers
        current = self.head(variant["id"])
        if current is None:
            return None, barriers
        owner = payload["review"]["id"] if kind in ("review.add", "review.correct") else ""
        if kind == "review.correct":
            matches_unowned = not current["owner"] and self.same_variant(current["variant"], payload["previousVariant"])
            if ((current["owner"] != owner and not matches_unowned)
                    or current["variant"]["reviewCount"] != variant["reviewCount"]):
                return None, barriers
            self.store(variant, owner, seq, event["id"])
            if self.is_graduated(current["variant"]) and not self.is_graduated(variant):
                barriers.add(variant["id"])
            return None, barriers
        old = current["variant"]
        old_graduated, new_graduated = self.is_graduated(old), self.is_graduated(variant)
        legacy_reset = (self.replay and "reset" not in payload and "graduated" not in variant
                        and variant["reviewCount"] == 0)
        reset = kind == "variant.upsert" and (payload.get("reset", False) or legacy_reset)
        if not reset and ((old_graduated and not new_graduated)
                          or (not old_graduated and new_graduated and old["reviewCount"] > variant["reviewCount"])):
            winner = dict(old if old_graduated else variant)
            winner["graduated"] = True
            winner["reviewCount"] = max(old["reviewCount"], variant["reviewCount"])
            self.store(winner, "", seq, event["id"])
            return {"variant": winner, "previous_seq": current["seq"], "previous_event_id": current["event_id"]}, barriers
        if kind == "variant.upsert" or variant["reviewCount"] >= old["reviewCount"]:
            self.store(variant, owner, seq, event["id"])
        if kind == "variant.upsert":
            barriers.add(variant["id"])
        return None, barriers

    def append_repair(self, source_event, conflict):
        existing = self.database.execute("SELECT resolution_seq FROM graduation_resolutions WHERE original_event_id=?",
                                         (source_event["id"],)).fetchone()
        if existing is not None:
            raise self.protocol_error("STORAGE_ERROR", f"Graduation resolution for event {source_event['id']} "
                                      f"already references sequence {existing[0]}. Preserve the account database and inspect its event log.", 500)
        identifier = str(uuid4())
        event = {"id": identifier, "deviceId": self.collection_id, "type": "variant.upsert",
                 "createdAt": source_event["createdAt"], "payload": {"variant": conflict["variant"]}}
        canonical = self.validate_event(event, self.collection_id)
        cursor = self.database.execute("INSERT INTO events(event_id,device_id,event_json,created_at) VALUES(?,?,?,?)",
                                       (identifier, self.collection_id, canonical, event["createdAt"]))
        seq = cursor.lastrowid
        self.database.execute("INSERT INTO graduation_resolutions VALUES(?,?)", (source_event["id"], seq))
        self.database.execute("INSERT INTO content_conflicts(seq,entity,entity_id,previous_seq,previous_event_id,event_id) "
                              "VALUES(?,'card',?,?,?,?)",
                              (seq, conflict["variant"]["cardId"], conflict["previous_seq"],
                               conflict["previous_event_id"], identifier))
        self.consume(event, seq)
