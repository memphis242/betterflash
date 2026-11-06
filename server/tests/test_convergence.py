from contextlib import closing
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
from uuid import uuid4

sys.path.insert(0, str(Path(__file__).parents[1]))
import main


class ConvergenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        root = Path(self.temporary.name)
        self.store = main.Store(root / "collection.sqlite", root / "media")
        self.device = str(uuid4())

    def tearDown(self):
        self.temporary.cleanup()

    def event(self, kind, payload):
        return {"id": str(uuid4()), "deviceId": self.device, "type": kind,
                "createdAt": "2026-10-20T00:00:00Z", "payload": payload}

    def deck(self, parent=""):
        return {"id": str(uuid4()), "name": "Deck", "description": "", "parentId": parent,
                "createdAt": "2026-10-20T00:00:00Z"}

    def sync(self, events=(), cursor=0):
        return self.store.sync({"deviceId": self.device, "cursor": cursor, "events": list(events)})

    def test_authoritative_scope_is_immutable_across_pages_retries_and_migration(self):
        parent, child = self.deck(), self.deck()
        card = {"id": str(uuid4()), "deckId": child["id"], "kind": "basic", "front": "Question",
                "back": "Answer", "tags": "", "pointCount": 1}
        variant = {"id": str(uuid4()), "cardId": card["id"], "key": "forward", "due": "2026-10-20T00:00:00Z",
                   "reviewCount": 0, "stability": 1, "difficulty": 0.5}
        # UUID v5 is the protocol's deterministic variant identity.
        from uuid import UUID, uuid5
        variant["id"] = str(uuid5(UUID(card["id"]), "forward"))
        initial = [self.event("deck.upsert", {"deck": parent}), self.event("deck.upsert", {"deck": child}),
                   self.event("card.upsert", {"card": card, "variants": [variant]})]
        self.sync(initial)
        moved = {**child, "parentId": parent["id"]}
        self.sync([self.event("deck.upsert", {"deck": moved})])
        delete = self.event("deck.delete", {"id": parent["id"]})
        response = self.sync([delete])
        deleted = next(row for row in response["events"] if row["event"]["id"] == delete["id"])
        expected = {parent["id"]: {"decks": {parent["id"]: "", child["id"]: parent["id"]},
                                    "cards": {card["id"]: child["id"]}}}
        self.assertEqual(deleted["deletionScopes"], expected)
        self.assertEqual(deleted["event"], delete)
        self.sync([self.event("deck.upsert", {"deck": child})])
        retry = self.sync([delete], deleted["seq"] - 1)
        self.assertEqual(retry["events"][0], deleted)
        with patch.object(main, "RESPONSE_LIMIT", 1):
            page = self.sync(cursor=deleted["seq"] - 1)
        self.assertEqual(page["events"], [deleted])
        self.assertTrue(page["hasMore"])
        with closing(self.store.connect()) as database:
            canonical = database.execute("SELECT seq,event_json FROM events ORDER BY seq").fetchall()
            database.execute("DELETE FROM projection_versions")
            database.execute("DELETE FROM deletion_scopes")
            database.execute("DELETE FROM card_locations")
        self.store = main.Store(self.store.path, self.store.media)
        self.assertEqual(self.sync(cursor=deleted["seq"] - 1)["events"][0], deleted)
        with closing(self.store.connect()) as database:
            self.assertEqual([tuple(row) for row in database.execute("SELECT seq,event_json FROM events ORDER BY seq")],
                             [tuple(row) for row in canonical])

    def test_scope_limit_rejection_rolls_back_event_and_projection(self):
        parent, child = self.deck(), self.deck()
        child["parentId"] = parent["id"]
        initial = self.sync([self.event("deck.upsert", {"deck": parent}), self.event("deck.upsert", {"deck": child})])
        delete = self.event("deck.delete", {"id": parent["id"]})
        with patch.object(main, "DELETION_SCOPE_BYTES", 1):
            with self.assertRaises(main.ProtocolError) as failure:
                self.sync([delete], initial["cursor"])
        self.assertEqual(failure.exception.code, "DELETION_SCOPE_LIMIT")
        with closing(self.store.connect()) as database:
            self.assertEqual(database.execute("SELECT COUNT(*) FROM events").fetchone()[0], 2)
            self.assertEqual(database.execute("SELECT SUM(deleted) FROM deck_parents").fetchone()[0], 0)
        self.assertEqual(self.sync([delete], initial["cursor"])["acceptedIds"], [delete["id"]])

    def test_scope_bytes_participate_in_paging_budget(self):
        parent = self.deck()
        children = [self.deck(parent["id"]) for _ in range(12)]
        initial = self.sync([self.event("deck.upsert", {"deck": deck}) for deck in [parent, *children]])
        delete = self.event("deck.delete", {"id": parent["id"]})
        following = self.event("deck.upsert", {"deck": self.deck()})
        with patch.object(main, "RESPONSE_BYTES", 1000):
            response = self.sync([delete, following], initial["cursor"])
        self.assertEqual(len(response["events"]), 1)
        self.assertGreater(len(json.dumps(response["events"][0])), 1000)
        self.assertTrue(response["hasMore"])
        self.assertEqual(self.sync(cursor=response["cursor"])["events"][0]["event"], following)

    def card_event(self, deck_id, card_id=None):
        from uuid import UUID, uuid5
        card = {"id": card_id or str(uuid4()), "deckId": deck_id, "kind": "basic", "front": "Question",
                "back": "Answer", "tags": "", "pointCount": 1}
        variant = {"id": str(uuid5(UUID(card["id"]), "forward")), "cardId": card["id"], "key": "forward",
                   "due": "2026-10-20T00:00:00Z", "reviewCount": 0, "stability": 1, "difficulty": 0.5}
        return self.event("card.upsert", {"card": card, "variants": [variant]})

    def test_rejected_moves_and_new_cards_get_idempotent_ordered_deletions(self):
        parent, other = self.deck(), self.deck()
        initial_card = self.card_event(other["id"])
        card_id = initial_card["payload"]["card"]["id"]
        initial = self.sync([self.event("deck.upsert", {"deck": parent}), self.event("deck.upsert", {"deck": other}), initial_card])
        self.sync([self.event("deck.delete", {"id": parent["id"]})], initial["cursor"])
        move = self.card_event(parent["id"], card_id)
        for event in [move, self.card_event(parent["id"])]:
            before = self.sync(cursor=0)["cursor"]
            page = self.sync([event], before)
            self.assertEqual([row["event"]["type"] for row in page["events"]], ["card.upsert", "card.delete"])
            generated = page["events"][1]["event"]
            self.assertEqual(generated["deviceId"], self.store.collection_id)
            self.assertEqual(generated["payload"]["id"], event["payload"]["card"]["id"])
            self.assertEqual(self.sync([event], before)["events"], page["events"])
            with closing(self.store.connect()) as database:
                head = database.execute("SELECT event_id,content_json FROM content_heads WHERE entity='card' AND entity_id=?", (generated["payload"]["id"],)).fetchone()
                self.assertEqual(tuple(head), (generated["id"], None))
        move_out = self.card_event(other["id"], card_id)
        self.sync([move_out])
        with closing(self.store.connect()) as database:
            self.assertEqual(database.execute("SELECT deleted FROM card_locations WHERE card_id=?", (card_id,)).fetchone()[0], 1)
            self.assertEqual(database.execute("SELECT COUNT(*) FROM card_resolutions").fetchone()[0], 2)
        restore = self.card_event(other["id"], card_id)
        restore["payload"].update({"restore": True, "freshVariantIds": []})
        self.sync([restore])
        self.store = main.Store(self.store.path, self.store.media)
        with closing(self.store.connect()) as database:
            self.assertEqual(database.execute("SELECT COUNT(*) FROM card_resolutions").fetchone()[0], 2)
            self.assertEqual(database.execute("SELECT deleted FROM card_locations WHERE card_id=?", (card_id,)).fetchone()[0], 0)

    def test_rejected_card_deletion_rolls_back_with_its_source_batch(self):
        import copy
        parent, other = self.deck(), self.deck()
        card = self.card_event(other["id"])
        initial = self.sync([self.event("deck.upsert", {"deck": parent}), self.event("deck.upsert", {"deck": other}), card,
                             self.event("deck.delete", {"id": parent["id"]})])
        card_id = card["payload"]["card"]["id"]
        move = self.card_event(parent["id"], card_id)
        conflict = copy.deepcopy(card)
        conflict["payload"]["card"]["front"] = "Different immutable content"
        with self.assertRaises(main.Conflict):
            self.sync([move, conflict], initial["cursor"])
        with closing(self.store.connect()) as database:
            self.assertEqual(database.execute("SELECT MAX(seq) FROM events").fetchone()[0], initial["cursor"])
            self.assertEqual(database.execute("SELECT COUNT(*) FROM card_resolutions").fetchone()[0], 0)
            self.assertEqual(database.execute("SELECT COUNT(*) FROM content_conflicts").fetchone()[0], 0)
            self.assertEqual(database.execute("SELECT deleted FROM card_locations WHERE card_id=?", (card_id,)).fetchone()[0], 0)
            self.assertEqual(database.execute("SELECT event_id FROM content_heads WHERE entity='card' AND entity_id=?", (card_id,)).fetchone()[0], card["id"])
        self.assertEqual(len(self.sync([move], initial["cursor"])["events"]), 2)

    def test_rejected_card_notice_belongs_to_deletion_page_and_survives_restart(self):
        parent, other = self.deck(), self.deck()
        card = self.card_event(other["id"])
        card_id = card["payload"]["card"]["id"]
        initial = self.sync([self.event("deck.upsert", {"deck": parent}), self.event("deck.upsert", {"deck": other}), card])
        self.sync([self.event("deck.delete", {"id": parent["id"]})], initial["cursor"])
        move = self.card_event(parent["id"], card_id)
        with patch.object(main, "RESPONSE_LIMIT", 1):
            page = self.sync([move], initial["cursor"])
            self.assertEqual(page["contentConflictCount"], 0)
            page = self.sync(cursor=page["cursor"])
            self.assertEqual(page["events"][0]["event"], move)
            self.assertEqual(page["contentConflictCount"], 0)
            before_deletion = page["cursor"]
            deletion = self.sync(cursor=before_deletion)
        self.assertEqual(deletion["events"][0]["event"]["type"], "card.delete")
        self.assertEqual(deletion["contentConflictCount"], 1)
        conflict = deletion["contentConflicts"][0]
        self.assertEqual((conflict["entity"], conflict["id"]), ("card", card_id))
        self.assertEqual(conflict["seq"], deletion["cursor"])
        self.assertEqual(conflict["previousSeq"], before_deletion)
        self.assertEqual(conflict["previousEventId"], move["id"])
        self.assertEqual(conflict["eventId"], deletion["events"][0]["event"]["id"])
        self.assertEqual(self.sync([move], before_deletion)["contentConflicts"], [conflict])
        self.store = main.Store(self.store.path, self.store.media)
        self.assertEqual(self.sync(cursor=before_deletion)["contentConflicts"], [conflict])
        after = self.sync(cursor=deletion["cursor"])
        self.assertEqual(after["contentConflictCount"], 0)
        self.assertEqual(after["contentConflicts"], [])

    def test_existing_generated_deletion_notices_backfill_without_rewriting_events(self):
        parent = self.deck()
        initial = self.sync([self.event("deck.upsert", {"deck": parent}),
                             self.event("deck.delete", {"id": parent["id"]})])
        event = self.card_event(parent["id"])
        page = self.sync([event], initial["cursor"])
        with closing(self.store.connect()) as database:
            canonical = [tuple(row) for row in database.execute("SELECT seq,event_json FROM events ORDER BY seq")]
            database.execute("DELETE FROM content_conflicts")
            database.execute("DELETE FROM projection_versions WHERE name='deleted-parent-card-notices-v1'")
        for _ in range(2):
            self.store = main.Store(self.store.path, self.store.media)
            replay = self.sync(cursor=initial["cursor"])
            self.assertEqual(replay["contentConflictCount"], 1)
            self.assertEqual(replay["contentConflicts"][0]["id"], event["payload"]["card"]["id"])
            self.assertEqual(replay["contentConflicts"][0]["seq"], page["cursor"])
            with closing(self.store.connect()) as database:
                self.assertEqual([tuple(row) for row in database.execute("SELECT seq,event_json FROM events ORDER BY seq")], canonical)
                database.execute("DELETE FROM projection_versions WHERE name='deleted-parent-cards-v1'")

    def test_legacy_rejection_backfill_preserves_later_restores_and_canonical_events(self):
        for restored in (False, True):
            with self.subTest(restored=restored):
                root = Path(self.temporary.name) / str(restored)
                store = main.Store(root / "collection.sqlite", root / "media")
                parent, other = self.deck(), self.deck()
                card = self.card_event(other["id"])
                card_id = card["payload"]["card"]["id"]
                events = [self.event("deck.upsert", {"deck": parent}), self.event("deck.upsert", {"deck": other}), card,
                          self.event("deck.delete", {"id": parent["id"]}), self.card_event(parent["id"], card_id)]
                if restored:
                    restore = self.card_event(other["id"], card_id)
                    restore["payload"].update({"restore": True, "freshVariantIds": []})
                    events.append(restore)
                with closing(store.connect()) as database:
                    for event in events:
                        canonical = main.validate_event(event, self.device)
                        database.execute("INSERT INTO events(event_id,device_id,event_json,created_at) VALUES(?,?,?,?)",
                                         (event["id"], self.device, canonical, event["createdAt"]))
                    database.execute("DELETE FROM projection_versions WHERE name='deleted-parent-cards-v1'")
                    before = [tuple(row) for row in database.execute("SELECT seq,event_json FROM events ORDER BY seq")]
                store = main.Store(store.path, store.media)
                with closing(store.connect()) as database:
                    after = [tuple(row) for row in database.execute("SELECT seq,event_json FROM events ORDER BY seq")]
                    self.assertEqual(after[:len(before)], before)
                    self.assertEqual(len(after), len(before) + (0 if restored else 1))
                    self.assertEqual(database.execute("SELECT deleted FROM card_locations WHERE card_id=?", (card_id,)).fetchone()[0], int(not restored))
                again = main.Store(store.path, store.media)
                with closing(again.connect()) as database:
                    self.assertEqual([tuple(row) for row in database.execute("SELECT seq,event_json FROM events ORDER BY seq")], after)

    def test_client_cannot_supply_authoritative_scope(self):
        parent = self.deck()
        self.sync([self.event("deck.upsert", {"deck": parent})])
        delete = self.event("deck.delete", {"id": parent["id"]})
        delete["deletionScopes"] = {"forged": "scope"}
        with self.assertRaises(main.ProtocolError) as failure:
            self.sync([delete])
        self.assertEqual(failure.exception.code, "INVALID_EVENT")
        del delete["deletionScopes"]
        delete["payload"]["deletionScopes"] = {}
        with self.assertRaises(main.ProtocolError):
            self.sync([delete])


if __name__ == "__main__":
    unittest.main()
