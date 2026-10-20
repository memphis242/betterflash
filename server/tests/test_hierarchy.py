from contextlib import closing
import json
from pathlib import Path
import sqlite3
import sys
import tempfile
import threading
import unittest
import urllib.error
import urllib.request
from http.server import ThreadingHTTPServer
from uuid import uuid4

sys.path.insert(0, str(Path(__file__).parents[1]))
import main


class HierarchyTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        main.TOKEN = "hierarchy-test-token-" + "x" * 32
        handler = type("TestHandler", (main.Handler,), {})
        handler.accounts = None
        handler.store = main.Store(Path(self.temporary.name) / "db", Path(self.temporary.name) / "media")
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.base = f"http://127.0.0.1:{self.server.server_port}"

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=5)
        self.temporary.cleanup()

    def request(self, body):
        headers = {"Authorization": "Bearer " + main.TOKEN, "X-BetterFlash-Protocol": "2"}
        request = urllib.request.Request(self.base + "/v1/sync", json.dumps(body).encode(), headers, method="POST")
        try:
            with urllib.request.urlopen(request, timeout=5) as response:
                return response.status, json.loads(response.read())
        except urllib.error.HTTPError as failure:
            with failure:
                return failure.code, json.loads(failure.read())

    @staticmethod
    def event(deck_id, parent_id, device_id, name):
        return {"id": str(uuid4()), "deviceId": device_id, "type": "deck.upsert",
                "payload": {"deck": {"id": deck_id, "parentId": parent_id, "name": name,
                                      "description": "", "createdAt": "2026-01-01T00:00:00Z"}},
                "createdAt": "2026-01-01T00:00:00Z"}

    def test_same_batch_cycle_gets_repaired_and_retry_does_not_duplicate(self):
        device = str(uuid4())
        first, second = str(uuid4()), str(uuid4())
        events = [self.event(first, second, device, "first"), self.event(second, first, device, "second")]
        body = {"deviceId": device, "cursor": 0, "events": events}
        status, response = self.request(body)
        self.assertEqual(status, 200)
        self.assertEqual(response["acceptedIds"], [event["id"] for event in events])
        self.assertEqual(response["hierarchyResolutionCount"], 1)
        repairs = [row["event"] for row in response["events"] if row["event"]["payload"].get("resolvedEventId")]
        self.assertEqual(len(repairs), 1)
        self.assertEqual(repairs[0]["payload"]["deck"]["id"], second)
        self.assertEqual(repairs[0]["payload"]["deck"]["parentId"], "")
        status, retry = self.request(body)
        self.assertEqual(status, 200)
        self.assertEqual(retry["hierarchyResolutionCount"], 1)
        self.assertEqual(len([row for row in retry["events"] if row["event"]["payload"].get("resolvedEventId")]), 1)
        device = str(uuid4())
        first, second = str(uuid4()), str(uuid4())
        reverse = [self.event(second, first, device, "second-first"), self.event(first, second, device, "first-second")]
        status, reverse_response = self.request({"deviceId": device, "cursor": retry["cursor"], "events": reverse})
        self.assertEqual(status, 200)
        self.assertEqual(reverse_response["hierarchyResolutionCount"], 1)
        reverse_repairs = [row["event"] for row in reverse_response["events"] if row["event"]["payload"].get("resolvedEventId")]
        self.assertEqual(len(reverse_repairs), 1)
        self.assertEqual(reverse_repairs[0]["payload"]["deck"]["id"], first)

    def test_missing_parent_does_not_trigger_repair(self):
        device = str(uuid4())
        event = self.event(str(uuid4()), str(uuid4()), device, "orphan")
        status, response = self.request({"deviceId": device, "cursor": 0, "events": [event]})
        self.assertEqual(status, 200)
        self.assertEqual(response["hierarchyResolutionCount"], 0)

    def test_delete_marks_descendants_and_restore_controls_resurrection(self):
        device = str(uuid4())
        parent, child = str(uuid4()), str(uuid4())
        events = [self.event(parent, "", device, "parent"), self.event(child, parent, device, "child")]
        status, response = self.request({"deviceId": device, "cursor": 0, "events": events})
        self.assertEqual(status, 200)
        delete = {"id": str(uuid4()), "deviceId": device, "type": "deck.delete",
                  "payload": {"id": parent}, "createdAt": "2026-01-02T00:00:00Z"}
        status, response = self.request({"deviceId": device, "cursor": response["cursor"], "events": [delete]})
        self.assertEqual(status, 200)
        with closing(self.server.RequestHandlerClass.store.connect()) as database:
            rows = database.execute("SELECT deck_id,deleted FROM deck_parents WHERE deck_id IN (?,?) ORDER BY deck_id", (parent, child)).fetchall()
        self.assertEqual([row[1] for row in rows], [1, 1])
        stale = self.event(child, "", device, "stale")
        status, response = self.request({"deviceId": device, "cursor": response["cursor"], "events": [stale]})
        self.assertEqual(status, 200)
        with closing(self.server.RequestHandlerClass.store.connect()) as database:
            self.assertEqual(database.execute("SELECT deleted FROM deck_parents WHERE deck_id=?", (child,)).fetchone()[0], 1)
        restore = self.event(child, "", device, "restore")
        restore["payload"]["restore"] = True
        status, response = self.request({"deviceId": device, "cursor": response["cursor"], "events": [restore]})
        self.assertEqual(status, 200)
        with closing(self.server.RequestHandlerClass.store.connect()) as database:
            self.assertEqual(database.execute("SELECT deleted FROM deck_parents WHERE deck_id=?", (child,)).fetchone()[0], 0)

    def test_restore_dependency_cannot_be_overridden_by_resolved_event_id(self):
        device = str(uuid4())
        parent, child = str(uuid4()), str(uuid4())
        events = [self.event(parent, "", device, "parent"), self.event(child, parent, device, "child")]
        status, response = self.request({"deviceId": device, "cursor": 0, "events": events})
        self.assertEqual(status, 200)
        malicious = self.event(child, "", device, "malicious")
        malicious["payload"]["restore"] = True
        malicious["payload"]["restoreDependency"] = True
        malicious["payload"]["resolvedEventId"] = str(uuid4())
        status, response = self.request({"deviceId": device, "cursor": response["cursor"], "events": [malicious]})
        self.assertEqual(status, 200)
        with closing(self.server.RequestHandlerClass.store.connect()) as database:
            self.assertEqual(database.execute("SELECT parent_id FROM deck_parents WHERE deck_id=?", (child,)).fetchone()[0], parent)

    def test_history_cycle_emits_repair(self):
        device = str(uuid4())
        first, second = str(uuid4()), str(uuid4())
        initial = [self.event(first, second, device, "first"), self.event(second, "", device, "second")]
        status, response = self.request({"deviceId": device, "cursor": 0, "events": initial})
        self.assertEqual(status, 200)
        before = {"id": second, "parentId": "", "name": "second", "description": "", "createdAt": "2026-01-01T00:00:00Z"}
        after = dict(before)
        after["parentId"] = first
        history = {"id": str(uuid4()), "deviceId": device, "type": "history.apply",
                   "payload": {"changes": [{"entity": "deck", "id": second, "before": before, "after": after}]},
                   "createdAt": "2026-01-02T00:00:00Z"}
        status, response = self.request({"deviceId": device, "cursor": response["cursor"], "events": [history]})
        self.assertEqual(status, 200)
        self.assertEqual(response["hierarchyResolutionCount"], 1)
        repairs = [row["event"] for row in response["events"] if row["event"]["payload"].get("resolvedEventId")]
        self.assertEqual(len(repairs), 1)
        self.assertEqual(repairs[0]["payload"]["deck"]["parentId"], "")
        self.assertTrue(repairs[0]["payload"]["restoreContainerOnly"])

    def test_startup_migration_does_not_append_stale_cycle_repair(self):
        root = Path(self.temporary.name)
        path = root / "legacy.sqlite3"
        device = str(uuid4())
        first, second = str(uuid4()), str(uuid4())
        events = [self.event(first, second, device, "first"), self.event(second, first, device, "cycle"),
                  self.event(second, "", device, "latest")]
        with closing(sqlite3.connect(path)) as database, database:
            database.execute("CREATE TABLE events(seq INTEGER PRIMARY KEY AUTOINCREMENT,event_id TEXT UNIQUE NOT NULL,device_id TEXT NOT NULL,event_json TEXT NOT NULL,created_at TEXT NOT NULL)")
            database.executemany("INSERT INTO events(event_id,device_id,event_json,created_at) VALUES(?,?,?,?)",
                                 [(event["id"], device, json.dumps(event, separators=(",", ":"), sort_keys=True), event["createdAt"]) for event in events])
        store = main.Store(path, root / "legacy-media")
        with closing(store.connect()) as database:
            self.assertEqual(database.execute("SELECT COUNT(*) FROM events").fetchone()[0], 3)
            self.assertEqual(database.execute("SELECT COUNT(*) FROM hierarchy_resolutions").fetchone()[0], 0)
            self.assertEqual(database.execute("SELECT parent_id FROM deck_parents WHERE deck_id=?", (second,)).fetchone()[0], "")

    def test_atomic_history_reparenting_is_not_repaired_for_transient_cycles(self):
        for reverse in (False, True):
            device = str(uuid4())
            first, second = str(uuid4()), str(uuid4())
            initial = [self.event(first, second, device, "first"), self.event(second, "", device, "second")]
            status, page = self.request({"deviceId": device, "cursor": 0, "events": initial})
            self.assertEqual(status, 200)
            changes = []
            for event, parent in ((initial[1], first), (initial[0], "")):
                before = event["payload"]["deck"]
                changes.append({"entity": "deck", "id": before["id"], "before": before,
                                "after": {**before, "parentId": parent}})
            if reverse:
                changes.reverse()
            event = {"id": str(uuid4()), "deviceId": device, "type": "history.apply",
                     "payload": {"changes": changes}, "createdAt": "2026-10-20T00:00:00Z"}
            status, page = self.request({"deviceId": device, "cursor": page["cursor"], "events": [event]})
            self.assertEqual(status, 200)
            self.assertEqual(page["hierarchyResolutionCount"], 0)
            with closing(self.server.RequestHandlerClass.store.connect()) as database:
                self.assertEqual(database.execute("SELECT parent_id FROM deck_parents WHERE deck_id=?", (first,)).fetchone()[0], "")
                self.assertEqual(database.execute("SELECT parent_id FROM deck_parents WHERE deck_id=?", (second,)).fetchone()[0], first)

    def test_history_deletion_cascades_after_children_are_moved_out(self):
        device = str(uuid4())
        parent, moved, removed = str(uuid4()), str(uuid4()), str(uuid4())
        initial = [self.event(parent, "", device, "parent"), self.event(moved, parent, device, "moved"),
                   self.event(removed, parent, device, "removed")]
        status, page = self.request({"deviceId": device, "cursor": 0, "events": initial})
        self.assertEqual(status, 200)
        parent_before = initial[0]["payload"]["deck"]
        moved_before = initial[1]["payload"]["deck"]
        changes = [{"entity": "deck", "id": parent, "before": parent_before, "after": None},
                   {"entity": "deck", "id": moved, "before": moved_before, "after": {**moved_before, "parentId": ""}}]
        event = {"id": str(uuid4()), "deviceId": device, "type": "history.apply",
                 "payload": {"changes": changes}, "createdAt": "2026-10-20T00:00:00Z"}
        status, page = self.request({"deviceId": device, "cursor": page["cursor"], "events": [event]})
        self.assertEqual(status, 200)
        self.assertEqual(page["hierarchyResolutionCount"], 0)
        with closing(self.server.RequestHandlerClass.store.connect()) as database:
            rows = {row["deck_id"]: row for row in database.execute("SELECT deck_id,parent_id,deleted FROM deck_parents")}
            self.assertEqual(rows[parent]["deleted"], 1)
            self.assertEqual(rows[removed]["deleted"], 1)
            self.assertEqual(rows[moved]["deleted"], 0)
            self.assertEqual(rows[moved]["parent_id"], "")

    def test_stale_move_into_deleted_parent_restores_the_previous_location(self):
        device = str(uuid4())
        parent, moved, child = str(uuid4()), str(uuid4()), str(uuid4())
        initial = [self.event(parent, "", device, "parent"), self.event(moved, "", device, "moved")]
        status, page = self.request({"deviceId": device, "cursor": 0, "events": initial})
        self.assertEqual(status, 200)
        deletion = {"id": str(uuid4()), "deviceId": device, "type": "deck.delete",
                    "payload": {"id": parent}, "createdAt": "2026-10-20T00:00:00Z"}
        status, page = self.request({"deviceId": device, "cursor": page["cursor"], "events": [deletion]})
        self.assertEqual(status, 200)
        stale = self.event(moved, parent, str(uuid4()), "offline metadata")
        status, page = self.request({"deviceId": stale["deviceId"], "cursor": 2, "events": [stale]})
        self.assertEqual(status, 200)
        self.assertEqual(page["hierarchyResolutionCount"], 1)
        repair = page["events"][-1]["event"]["payload"]
        self.assertEqual(repair["deck"]["parentId"], "")
        self.assertTrue(repair["restoreContainerOnly"])
        status, page = self.request({"deviceId": device, "cursor": page["cursor"],
                                    "events": [self.event(child, moved, device, "child")]})
        self.assertEqual(status, 200)
        with closing(self.server.RequestHandlerClass.store.connect()) as database:
            rows = {row["deck_id"]: row for row in database.execute("SELECT deck_id,parent_id,deleted FROM deck_parents")}
            self.assertEqual(rows[parent]["deleted"], 1)
            self.assertEqual(rows[moved]["deleted"], 0)
            self.assertEqual(rows[moved]["parent_id"], "")
            self.assertEqual(rows[child]["parent_id"], moved)


if __name__ == "__main__":
    unittest.main()
