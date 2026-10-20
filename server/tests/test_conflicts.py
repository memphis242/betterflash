from contextlib import closing
import copy
from pathlib import Path
import sys
import tempfile
import unittest
from uuid import uuid4

sys.path.insert(0, str(Path(__file__).parents[1]))
import main


class ConflictTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        root = Path(self.temporary.name)
        self.store = main.Store(root / "collection.sqlite", root / "media")
        self.device = str(uuid4())

    def tearDown(self):
        self.temporary.cleanup()

    def event(self, kind, payload, device=None):
        return {"id": str(uuid4()), "deviceId": device or self.device, "type": kind,
                "createdAt": "2026-10-20T00:00:00Z", "payload": payload}

    def deck(self):
        return {"id": str(uuid4()), "name": "Offline deck", "description": "",
                "createdAt": "2026-10-20T00:00:00Z"}

    def sync(self, events, cursor=0, device=None):
        return self.store.sync({"deviceId": device or self.device, "cursor": cursor, "events": events})

    def test_more_than_a_thousand_conflicts_have_an_exact_count_and_idempotent_journal(self):
        decks = [self.deck() for _ in range(1001)]
        initial = self.event("history.apply", {"changes": [
            {"entity": "deck", "id": deck["id"], "before": None, "after": deck} for deck in decks]})
        self.sync([initial])
        other_device = str(uuid4())
        changes = []
        for deck in decks:
            edited = {**deck, "description": "Concurrent offline edit"}
            changes.append({"entity": "deck", "id": deck["id"], "before": deck, "after": edited})
        concurrent = self.event("history.apply", {"changes": changes}, other_device)
        page = self.sync([concurrent], device=other_device)
        self.assertEqual(page["contentConflictCount"], 1001)
        self.assertEqual(len(page["contentConflicts"]), 1000)
        retry = self.sync([concurrent], device=other_device)
        self.assertEqual(retry["contentConflictCount"], 1001)
        with closing(self.store.connect()) as database:
            self.assertEqual(database.execute("SELECT COUNT(*) FROM content_conflicts").fetchone()[0], 1001)
        restarted = main.Store(self.store.path, self.store.media)
        self.assertEqual(restarted.collection_id, self.store.collection_id)
        self.assertEqual(restarted.sync({"deviceId": other_device, "cursor": 0, "events": []})["contentConflictCount"], 1001)

    def test_cloned_device_reports_conflicts_but_ordered_writes_in_one_batch_do_not(self):
        deck = self.deck()
        original = self.event("deck.upsert", {"deck": deck})
        changed = self.event("deck.upsert", {"deck": {**deck, "name": "Second edit"}})
        self.assertEqual(self.sync([original, changed])["contentConflictCount"], 0)
        cloned = self.event("deck.upsert", {"deck": {**deck, "name": "Cloned offline edit"}})
        self.assertEqual(self.sync([cloned])["contentConflictCount"], 1)

    def test_invalid_batch_rolls_back_heads_and_conflicts(self):
        deck = self.deck()
        original = self.event("deck.upsert", {"deck": deck})
        self.sync([original])
        other_device = str(uuid4())
        concurrent = self.event("deck.upsert", {"deck": {**deck, "name": "Concurrent"}}, other_device)
        reused = copy.deepcopy(original)
        reused["deviceId"] = other_device
        with self.assertRaises(main.Conflict):
            self.sync([concurrent, reused], device=other_device)
        with closing(self.store.connect()) as database:
            self.assertEqual(database.execute("SELECT COUNT(*) FROM content_conflicts").fetchone()[0], 0)
            self.assertEqual(database.execute("SELECT event_id FROM content_heads").fetchone()[0], original["id"])

    def test_settings_and_invalid_event_type_are_rejected(self):
        for kind in ("settings.upsert", [], {}):
            with self.assertRaises(main.ProtocolError):
                self.sync([self.event(kind, {})])
