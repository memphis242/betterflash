import hashlib
import json
from pathlib import Path
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


def digest(token):
    return hashlib.sha256(token.encode()).hexdigest()


class AccountTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        root = Path(self.temporary.name)
        self.accounts = {
            "accounts": [
                {"id": "11111111-1111-4111-8111-111111111111", "tokenSha256": [digest("alpha-token")]},
                {"id": "22222222-2222-4222-8222-222222222222", "tokenSha256": [digest("beta-token"), digest("beta-old")]},
            ]
        }
        self.accounts_path = root / "accounts.json"
        self.accounts_path.write_text(json.dumps(self.accounts))
        handler = type("TestHandler", (main.Handler,), {})
        handler.store = None
        handler.accounts = main.Accounts(self.accounts_path, root / "data")
        self.stores = handler.accounts.stores
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.base = f"http://127.0.0.1:{self.server.server_port}"

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=5)
        self.temporary.cleanup()

    def request(self, method, path, data=None, token="alpha-token", raw=False):
        headers = {"Authorization": "Bearer " + token, "X-BetterFlash-Protocol": "2"}
        if method == "PUT":
            account_id = "22222222-2222-4222-8222-222222222222" if token.startswith("beta") else "11111111-1111-4111-8111-111111111111"
            headers["X-BetterFlash-Collection"] = self.stores[account_id].collection_id
        body = data if raw else json.dumps(data).encode() if data is not None else None
        if not raw and data is not None:
            headers["Content-Type"] = "application/json"
        request = urllib.request.Request(self.base + path, body, headers, method=method)
        try:
            with urllib.request.urlopen(request, timeout=5) as response:
                output = response.read()
                return response.status, output if raw else json.loads(output)
        except urllib.error.HTTPError as failure:
            with failure:
                return failure.code, json.loads(failure.read())

    def event(self, device="33333333-3333-4333-8333-333333333333"):
        return {"id": str(uuid4()), "deviceId": device, "type": "deck.upsert",
                "payload": {"deck": {"id": str(uuid4()), "name": "A deck", "description": "",
                                      "createdAt": "2026-01-01T00:00:00Z"}},
                "createdAt": "2026-01-01T00:00:00Z"}

    def sync(self, events, token="alpha-token", cursor=0, device="33333333-3333-4333-8333-333333333333"):
        return self.request("POST", "/v1/sync", {"deviceId": device,
                                                   "cursor": cursor, "events": events}, token)

    def test_accounts_have_independent_logs_and_media(self):
        event = self.event()
        self.assertEqual(self.sync([event])[0], 200)
        status, page = self.sync([], token="beta-token")
        self.assertEqual(status, 200)
        self.assertEqual(page["events"], [])
        data = b"account alpha media"
        filename = hashlib.sha256(data).hexdigest() + ".png"
        self.assertEqual(self.request("PUT", "/v1/media/" + filename, data, raw=True)[0], 201)
        self.assertEqual(self.request("GET", "/v1/media/" + filename, token="beta-token")[0], 404)
        self.assertEqual(self.request("GET", "/v1/media/" + filename, raw=True)[1], data)
        self.assertEqual(self.sync([], token="beta-old")[0], 200)

    def test_media_upload_requires_matching_collection(self):
        data = b"bound media"
        filename = hashlib.sha256(data).hexdigest() + ".png"
        headers = {"Authorization": "Bearer alpha-token", "X-BetterFlash-Protocol": "2",
                   "X-BetterFlash-Collection": self.stores["22222222-2222-4222-8222-222222222222"].collection_id}
        request = urllib.request.Request(self.base + "/v1/media/" + filename, data, headers, method="PUT")
        with self.assertRaises(urllib.error.HTTPError) as raised:
            urllib.request.urlopen(request, timeout=5)
        self.assertEqual(raised.exception.code, 409)
        raised.exception.close()
        self.assertFalse((self.stores["11111111-1111-4111-8111-111111111111"].media / filename).exists())

    def test_info_identity_is_stable_and_collection_mismatch_is_atomic(self):
        status, info = self.request("GET", "/v1/info")
        self.assertEqual(status, 200)
        self.assertEqual(info["protocolVersion"], 2)
        collection_id = info["collectionId"]
        self.assertEqual(collection_id, self.stores["11111111-1111-4111-8111-111111111111"].collection_id)
        event = self.event()
        status, response = self.sync([event])
        self.assertEqual(status, 200)
        self.assertEqual(response["collectionId"], collection_id)
        mismatch = self.request("POST", "/v1/sync", {"deviceId": event["deviceId"], "cursor": 0,
                                                        "collectionId": "22222222-2222-4222-8222-222222222222",
                                                        "events": [self.event()]})
        self.assertEqual(mismatch[0], 409)
        self.assertEqual(self.sync([], cursor=0)[1]["cursor"], 1)
        restarted = main.Store(self.stores["11111111-1111-4111-8111-111111111111"].path,
                               self.stores["11111111-1111-4111-8111-111111111111"].media)
        self.assertEqual(restarted.collection_id, collection_id)

    def test_wrong_credentials_are_rejected(self):
        status, body = self.sync([], token="wrong")
        self.assertEqual(status, 401)
        self.assertEqual(body["error"]["code"], "UNAUTHORIZED")

    def test_offline_content_edits_report_conflict_but_review_does_not(self):
        first = self.event()
        self.assertEqual(self.sync([first])[0], 200)
        second = self.event("44444444-4444-4444-8444-444444444444")
        second["payload"]["deck"]["id"] = first["payload"]["deck"]["id"]
        second["payload"]["deck"]["name"] = "Edited offline"
        status, response = self.sync([second], token="alpha-token", device=second["deviceId"])
        self.assertEqual(status, 200)
        conflicts = response["contentConflicts"]
        self.assertEqual(len(conflicts), 1)
        self.assertEqual(conflicts[0]["entity"], "deck")
        self.assertEqual(conflicts[0]["id"], first["payload"]["deck"]["id"])
        review = self.event("44444444-4444-4444-8444-444444444444")
        review["type"] = "review.add"
        card_id, review_id = str(uuid4()), str(uuid4())
        variant_id = str(uuid4())
        review["payload"] = {"review": {"id": review_id, "cardId": card_id, "variantId": variant_id,
                                         "deckName": "A deck", "grade": 3, "recallFraction": 1.0,
                                         "responseSeconds": 1.0, "reviewedAt": "2026-01-01T00:00:00Z",
                                         "due": "2026-01-01T00:00:00Z"}, "historyOnly": True}
        status, review_response = self.sync([review], token="alpha-token", device=review["deviceId"])
        self.assertEqual(status, 200)
        self.assertTrue(all(conflict["entity"] == "deck" for conflict in review_response["contentConflicts"]))

    def test_accounts_validation_fails_closed(self):
        root = Path(self.temporary.name)
        cases = [
            {"accounts": [{"id": "not-a-uuid", "tokenSha256": [digest("x")]}]},
            {"accounts": [{"id": "11111111-1111-4111-8111-111111111111", "tokenSha256": ["A" * 64]}]},
            {"accounts": [{"id": "11111111-1111-4111-8111-111111111111", "tokenSha256": []}]},
            {"accounts": [{"id": "11111111-1111-4111-8111-111111111111", "tokenSha256": [digest("x")]},
                           {"id": "11111111-1111-4111-8111-111111111111", "tokenSha256": [digest("y")]}]},
            {"accounts": [{"id": "11111111-1111-4111-8111-111111111111", "tokenSha256": [digest("x")]},
                           {"id": "22222222-2222-4222-8222-222222222222", "tokenSha256": [digest("x")]}]},
        ]
        for index, document in enumerate(cases):
            path = root / f"invalid-{index}.json"
            path.write_text(json.dumps(document))
            with self.assertRaises(SystemExit):
                main.Accounts(path, root / f"invalid-data-{index}")


if __name__ == "__main__":
    unittest.main()
