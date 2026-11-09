import hashlib
import json
from pathlib import Path
import sys
import tempfile
import threading
import unittest
import urllib.error
import urllib.request
from uuid import UUID, uuid4, uuid5

sys.path.insert(0, str(Path(__file__).parents[1]))
import main
from media_fixture import png_bytes
from http.server import ThreadingHTTPServer

TOKEN = "local-test-token-" + "x" * 32


class SyncTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        main.TOKEN = TOKEN
        self.device = str(uuid4())
        handler = type("TestHandler", (main.Handler,), {})
        handler.store = main.Store(Path(self.temporary.name) / "db", Path(self.temporary.name) / "media")
        self.store = handler.store
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.base = f"http://127.0.0.1:{self.server.server_port}"

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=5)
        self.temporary.cleanup()

    def request(self, method, path, data=None, token=TOKEN, raw=False):
        headers = {"Authorization": "Bearer " + token, "X-BetterFlash-Protocol": "5"}
        if method == "PUT":
            headers["X-BetterFlash-Collection"] = self.store.collection_id
        body = data if raw else json.dumps(data).encode() if data is not None else None
        if not raw and data is not None:
            headers["Content-Type"] = "application/json"
        request = urllib.request.Request(self.base + path, body, headers, method=method)
        try:
            with urllib.request.urlopen(request, timeout=5) as response:
                output = response.read()
                return response.status, output if raw or method == "HEAD" else json.loads(output)
        except urllib.error.HTTPError as failure:
            with failure:
                return failure.code, json.loads(failure.read())

    def event(self):
        return {"id": str(uuid4()), "deviceId": self.device, "type": "deck.upsert",
                "payload": {"deck": {"id": str(uuid4()), "name": "A deck", "description": "",
                                      "createdAt": "2026-01-01T00:00:00Z"}},
                "createdAt": "2026-01-01T00:00:00Z"}

    def sync(self, events, cursor=0):
        return self.request("POST", "/v1/sync", {"deviceId": self.device, "cursor": cursor, "events": events})

    def test_retry_acknowledges_same_uuid_without_duplicate(self):
        event = self.event()
        status, first = self.sync([event])
        self.assertEqual(status, 200)
        status, retry = self.sync([event])
        self.assertEqual(status, 200)
        self.assertEqual(retry["acceptedIds"], [event["id"]])
        self.assertEqual(len(retry["events"]), 1)
        self.assertEqual(first["cursor"], retry["cursor"])

    def test_conflicting_id_rolls_back_entire_batch(self):
        event = self.event()
        self.assertEqual(self.sync([event])[0], 200)
        different = json.loads(json.dumps(event))
        different["payload"]["deck"]["name"] = "Different"
        new = self.event()
        self.assertEqual(self.sync([new, different])[0], 409)
        self.assertEqual(len(self.sync([])[1]["events"]), 1)

    def test_validation_auth_and_routes(self):
        self.assertEqual(self.sync([], cursor=-1)[0], 400)
        self.assertEqual(self.sync([], cursor=1)[0], 400)
        event = self.event()
        event["deviceId"] = str(uuid4())
        self.assertEqual(self.sync([event])[0], 400)
        self.assertEqual(self.request("POST", "/v1/sync", {}, token="wrong")[0], 401)
        self.assertEqual(self.request("POST", "/unknown", {})[0], 404)
        self.assertEqual(self.request("PUT", "/unknown", b"image", raw=True)[0], 404)
        self.assertEqual(self.request("GET", "/health")[0], 200)

    def test_invalid_payloads_never_enter_the_event_log(self):
        for mutation in (lambda event: event["payload"]["deck"].update(name=""),
                         lambda event: event["payload"].update(deck=[]),
                         lambda event: event.update(createdAt="2026-01-01"),
                         lambda event: event["payload"]["deck"].update(id="00000000-0000-0000-0000-000000000000")):
            event = self.event()
            mutation(event)
            self.assertEqual(self.sync([self.event(), event])[0], 400)
            self.assertEqual(len(self.sync([])[1]["events"]), 0)
        self.assertEqual(self.request("POST", "/v1/sync", b'{"deviceId":"a","deviceId":"b"}', raw=True)[0], 400)

    def test_deck_parent_is_optional_uuid_and_not_self(self):
        root = self.event()
        root_id = root["payload"]["deck"]["id"]
        self.assertEqual(self.sync([root])[0], 200)
        child = self.event()
        child["payload"]["deck"]["parentId"] = root_id
        self.assertEqual(self.sync([child])[0], 200)
        invalid = self.event()
        invalid["payload"]["deck"]["parentId"] = None
        self.assertEqual(self.sync([invalid])[0], 400)
        invalid = self.event()
        invalid["payload"]["deck"]["parentId"] = invalid["payload"]["deck"]["id"]
        self.assertEqual(self.sync([invalid])[0], 400)
        missing = self.event()
        missing["payload"]["deck"].pop("parentId", None)
        self.assertEqual(self.sync([missing])[0], 200)

    def test_cloze_payload_accepts_code_scopes_and_multiline_answers(self):
        event = self.event()
        card_id = str(uuid4())
        event["type"] = "card.upsert"
        event["payload"] = {
            "card": {"id": card_id, "deckId": str(uuid4()), "kind": "cloze",
                     "front": "Use [cloze1:std::expected] with [cloze2:first\nsecond|two lines].",
                     "back": "", "tags": "", "pointCount": 1},
            "variants": [{"id": str(uuid5(UUID(card_id), key)), "cardId": card_id, "key": key,
                          "due": "2026-01-01T00:00:00Z", "reviewCount": 0,
                          "stability": 1.0, "difficulty": 0.5} for key in ("c1", "c2")]}
        self.assertEqual(self.sync([event])[0], 200)
        invalid = json.loads(json.dumps(event))
        invalid["id"] = str(uuid4())
        invalid["payload"]["card"]["front"] = "[cloze1:answer|]"
        self.assertEqual(self.sync([invalid])[0], 400)
        self.assertEqual(len(self.sync([])[1]["events"]), 1)

    def test_subday_variant_schedules_round_trip_and_reject_out_of_bounds(self):
        card_id = str(uuid4())
        variant_id = str(uuid5(UUID(card_id), "forward"))
        intervals = (10 / 1440, 1 / 24, 1 / 3, 1.0)
        events = []
        for stability in intervals:
            events.append({"id": str(uuid4()), "deviceId": self.device,
                           "type": "variant.upsert", "createdAt": "2026-11-08T00:00:00Z",
                           "payload": {"variant": {"id": variant_id, "cardId": card_id, "key": "forward",
                                                       "due": "2026-11-08T00:10:00Z", "reviewCount": 1,
                                                       "stability": stability, "difficulty": 0.5,
                                                       "graduated": stability >= 1.0}}})
        status, response = self.sync(events)
        self.assertEqual(status, 200)
        self.assertEqual(len(response["acceptedIds"]), len(intervals))
        self.assertEqual([page["event"]["payload"]["variant"]["graduated"]
                          for page in response["events"]], [False, False, False, True])

        legacy_event = {"id": str(uuid4()), "deviceId": self.device,
                        "type": "variant.upsert", "createdAt": "2026-11-08T00:00:00Z",
                        "payload": {"variant": {"id": variant_id, "cardId": card_id, "key": "forward",
                                                    "due": "2026-11-08T00:10:00Z", "reviewCount": 1,
                                                    "stability": 0.25, "difficulty": 0.5}}}
        status, legacy_response = self.sync([legacy_event])
        self.assertEqual(status, 200)
        self.assertNotIn("graduated", legacy_response["events"][-1]["event"]["payload"]["variant"])

        legacy_new = json.loads(json.dumps(legacy_event))
        legacy_new["id"] = str(uuid4())
        legacy_new["payload"]["variant"]["reviewCount"] = 0
        legacy_new["payload"]["variant"]["stability"] = 1.0
        status, legacy_new_response = self.sync([legacy_new])
        self.assertEqual(status, 200)
        self.assertNotIn("graduated", legacy_new_response["events"][-1]["event"]["payload"]["variant"])

        for invalid_stability in (9 / 1440, 3650.001):
            event = {"id": str(uuid4()), "deviceId": self.device,
                     "type": "variant.upsert", "createdAt": "2026-11-08T00:00:00Z",
                     "payload": {"variant": {"id": variant_id, "cardId": card_id, "key": "forward",
                                                 "due": "2026-11-08T00:10:00Z", "reviewCount": 1,
                                                 "stability": invalid_stability, "difficulty": 0.5}}}
            self.assertEqual(self.sync([event])[0], 400)

        for invalid_graduated in (1, "false", None):
            event = json.loads(json.dumps(legacy_event))
            event["id"] = str(uuid4())
            event["payload"]["variant"]["graduated"] = invalid_graduated
            self.assertEqual(self.sync([event])[0], 400)

        valid = json.loads(json.dumps(legacy_event))
        valid["id"] = str(uuid4())
        invalid_phase = json.loads(json.dumps(valid))
        invalid_phase["id"] = str(uuid4())
        invalid_phase["payload"]["variant"].update(reviewCount=1, stability=1.0, graduated=False)
        event_count = len(self.sync([])[1]["events"])
        self.assertEqual(self.sync([valid, invalid_phase])[0], 400)
        self.assertEqual(len(self.sync([])[1]["events"]), event_count)

        invalid_unreviewed_graduate = json.loads(json.dumps(legacy_event))
        invalid_unreviewed_graduate["id"] = str(uuid4())
        invalid_unreviewed_graduate["payload"]["variant"].update(reviewCount=0, stability=1.0, graduated=True)
        self.assertEqual(self.sync([invalid_unreviewed_graduate])[0], 400)

    def test_media_round_trip_and_hash_integrity(self):
        data = png_bytes()
        filename = hashlib.sha256(data).hexdigest() + ".png"
        self.assertEqual(self.request("PUT", "/v1/media/" + filename, b"wrong", raw=True)[0], 422)
        self.assertEqual(self.request("PUT", "/v1/media/" + filename, data, raw=True)[0], 201)
        status, received = self.request("GET", "/v1/media/" + filename, raw=True)
        self.assertEqual(status, 200)
        self.assertEqual(received, data)
        self.assertEqual(self.request("HEAD", "/v1/media/" + filename, raw=True), (200, b""))
        self.assertEqual(self.request("GET", "/v1/media/../db")[0], 404)
        self.assertEqual(self.request("GET", "/v1/media/" + filename, token="wrong")[0], 401)

    def test_media_symlinks_are_not_read_or_written(self):
        data = png_bytes(1)
        filename = hashlib.sha256(data).hexdigest() + ".png"
        private = Path(self.temporary.name) / "private"
        private.write_bytes(data)
        (self.store.media / filename).symlink_to(private)
        self.assertEqual(self.request("GET", "/v1/media/" + filename)[0], 404)
        self.assertEqual(self.request("PUT", "/v1/media/" + filename, data, raw=True)[0], 409)
        self.assertEqual(private.read_bytes(), data)

    def test_paginated_cursor_preserves_every_event(self):
        for _ in range(11):
            self.assertEqual(self.sync([self.event() for _ in range(100)])[0], 200)
        first = self.sync([])[1]
        self.assertEqual(len(first["events"]), 1000)
        self.assertTrue(first["hasMore"])
        second = self.sync([], first["cursor"])[1]
        self.assertEqual(len(second["events"]), 100)
        self.assertFalse(second["hasMore"])
        self.assertEqual(second["cursor"], 1100)
        self.assertEqual(len({row["event"]["id"] for row in first["events"] + second["events"]}), 1100)

    def test_large_response_pages_stay_within_client_memory_limit(self):
        for _ in range(5):
            events = [self.event() for _ in range(25)]
            for event in events:
                event["payload"]["deck"]["description"] = "documented concept " * 3300
            self.assertEqual(self.sync(events)[0], 200)
        cursor, received = 0, []
        for _ in range(5):
            status, page = self.sync([], cursor)
            self.assertEqual(status, 200)
            self.assertLess(len(json.dumps(page, separators=(",", ":")).encode()), 8 * 1024 * 1024)
            received.extend(row["event"]["id"] for row in page["events"])
            self.assertGreater(page["cursor"], cursor)
            cursor = page["cursor"]
            if not page["hasMore"]:
                break
        self.assertEqual(len(set(received)), 125)
        self.assertEqual(cursor, 125)

    def test_media_reference_budget_paginate_and_rejects_oversized_cards(self):
        names = []
        for index in range(80):
            data = png_bytes(index)
            name = hashlib.sha256(data).hexdigest() + ".png"
            (self.store.media / name).write_bytes(data)
            names.append(name)

        def card_event(references):
            event = self.event()
            identifier = str(uuid4())
            event["type"] = "card.upsert"
            event["payload"] = {
                "card": {"id": identifier, "deckId": str(uuid4()), "kind": "basic",
                         "front": "\n\n".join(f"![image](media:{name})" for name in references),
                         "back": "Answer", "tags": "", "pointCount": 1},
                "variants": [{"id": str(uuid5(UUID(identifier), "forward")), "cardId": identifier,
                              "key": "forward", "due": "2026-01-01T00:00:00Z",
                              "reviewCount": 0, "stability": 1.0, "difficulty": 0.5}]}
            return event

        self.assertEqual(self.sync([card_event(names[:65])])[0], 413)
        self.assertEqual(self.sync([card_event(["b" * 64 + ".png"])])[0], 422)
        status, first = self.sync([card_event(names[:40]), card_event(names[40:])])
        self.assertEqual(status, 200)
        self.assertEqual(len(first["acceptedIds"]), 2)
        self.assertEqual(len(first["events"]), 1)
        self.assertEqual(len(first["media"]), 40)
        self.assertTrue(first["hasMore"])
        second = self.sync([], first["cursor"])[1]
        self.assertEqual(len(second["events"]), 1)
        self.assertFalse(second["hasMore"])
        self.assertEqual(second["cursor"], 2)


if __name__ == "__main__":
    unittest.main()
