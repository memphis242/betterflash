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
