"""HTTP and failure-injection regressions for the sync service boundary."""
import hashlib
import io
import json
from pathlib import Path
import queue
import socket
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch
from uuid import UUID, uuid4, uuid5
import urllib.error
import urllib.request

from PIL import Image

sys.path.insert(0, str(Path(__file__).parents[1]))
import main
import provision


def png_bytes(color="red"):
    output = io.BytesIO()
    Image.new("RGB", (2, 2), color).save(output, "PNG")
    return output.getvalue()


class SecurityTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.registry = self.root / "accounts.json"
        self.registry.write_text(json.dumps({"accounts": [
            {"id": "11111111-1111-4111-8111-111111111111", "tokenSha256": [hashlib.sha256(b"alpha").hexdigest()]},
            {"id": "22222222-2222-4222-8222-222222222222", "tokenSha256": [hashlib.sha256(b"beta").hexdigest()]},
        ]}))
        self.ready = queue.Queue()
        ready = self.ready

        class TestHandler(main.Handler):
            def authorized(self, **kwargs):
                result = super().authorized(**kwargs)
                ready.put(result is not None)
                return result

        TestHandler.accounts = main.Accounts(self.registry, self.root / "state")
        self.store = TestHandler.accounts.authorize("Bearer alpha")
        self.handler = TestHandler
        self.server = main.BoundedHTTPServer(("127.0.0.1", 0), TestHandler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.base = f"http://127.0.0.1:{self.server.server_port}"

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(5)
        self.temporary.cleanup()

    def request(self, path, data=None, token="alpha", method="GET"):
        request = urllib.request.Request(self.base + path, data, {
            "Authorization": "Bearer " + token,
            "X-BetterFlash-Protocol": "4",
            "X-BetterFlash-Collection": self.store.collection_id,
        }, method=method)
        try:
            with urllib.request.urlopen(request, timeout=3) as response:
                return response.status, response.read()
        except urllib.error.HTTPError as failure:
            with failure:
                return failure.code, failure.read()

    def test_invalid_image_cannot_be_uploaded_or_admitted(self):
        for extension, data in (("png", b"not an image"), ("jpg", png_bytes()), ("png", png_bytes()[:40])):
            with self.subTest(extension=extension, length=len(data)):
                name = hashlib.sha256(data).hexdigest() + "." + extension
                status, _ = self.request("/v1/media/" + name, data, method="PUT")
                self.assertEqual(status, 422)
                self.assertFalse((self.store.media / name).exists())
        data = png_bytes()
        name = hashlib.sha256(data).hexdigest() + ".png"
        self.assertEqual(self.request("/v1/media/" + name, data, method="PUT")[0], 201)

    def test_supported_formats_and_pixel_budget(self):
        for extension, image_format in (("png", "PNG"), ("jpg", "JPEG"), ("jpeg", "JPEG"), ("gif", "GIF"), ("webp", "WEBP")):
            with self.subTest(extension=extension):
                output = io.BytesIO()
                Image.new("RGB", (2, 2), "blue").save(output, image_format)
                data = output.getvalue()
                name = hashlib.sha256(data).hexdigest() + "." + extension
                self.assertTrue(main.valid_media_bytes(name, data))
        # A valid PNG header advertises excessive dimensions before decoding data.
        import struct
        import zlib
        data = png_bytes()
        header = struct.pack(">IIBBBBB", 8000, 4001, 8, 2, 0, 0, 0)
        oversized = data[:16] + header + struct.pack(">I", zlib.crc32(b"IHDR" + header)) + data[33:]
        self.assertFalse(main.valid_media_bytes("0" * 64 + ".png", oversized))

    def test_old_invalid_media_cannot_poison_new_card_history(self):
        data = b"legacy invalid image"
        name = hashlib.sha256(data).hexdigest() + ".png"
        (self.store.media / name).write_bytes(data)
        card_id, device_id = str(uuid4()), str(uuid4())
        event = {"id": str(uuid4()), "deviceId": device_id, "type": "card.upsert",
                 "createdAt": "2026-01-01T00:00:00Z", "payload": {
                     "card": {"id": card_id, "deckId": str(uuid4()), "kind": "basic", "front": f"![image](media:{name})",
                              "back": "answer", "tags": "", "pointCount": 1},
                     "variants": [{"id": str(uuid5(UUID(card_id), "forward")), "cardId": card_id,
                                   "key": "forward", "due": "2026-01-01T00:00:00Z", "reviewCount": 0,
                                   "stability": 1.0, "difficulty": 0.5}]}}
        body = {"deviceId": device_id, "cursor": 0, "events": [event]}
        self.assertEqual(self.request("/v1/sync", json.dumps(body).encode(), method="POST")[0], 422)
        body["events"] = []
        status, response = self.request("/v1/sync", json.dumps(body).encode(), method="POST")
        self.assertEqual(status, 200)
        self.assertEqual(json.loads(response)["events"], [])

    def test_disabled_account_does_not_disable_other_accounts(self):
        registry = json.loads(self.registry.read_text())
        registry["accounts"][0]["tokenSha256"] = []
        self.registry.write_text(json.dumps(registry))
        accounts = main.Accounts(self.registry, self.root / "state")
        self.assertIsNone(accounts.authorize("Bearer alpha"))
        self.assertIsNotNone(accounts.authorize("Bearer beta"))
        self.assertEqual(len(accounts.stores), 2)

    def test_absolute_deadline_closes_partial_header_and_body(self):
        with patch.object(main, "REQUEST_SECONDS", 0.3, create=True):
            for payload in (b"POST /v1/sync HTTP/1.1\r\nHost: local\r\n", b"POST /v1/sync HTTP/1.1\r\nHost: local\r\nAuthorization: Bearer alpha\r\nContent-Length: 100\r\n\r\n{"):
                with self.subTest(payload=payload), socket.create_connection(self.server.server_address) as connection:
                    connection.settimeout(1)
                    connection.sendall(payload)
                    stopped = threading.Event()

                    def trickle():
                        while not stopped.wait(0.03):
                            try:
                                connection.sendall(b"x")
                            except OSError:
                                return

                    sender = threading.Thread(target=trickle)
                    sender.start()
                    try:
                        self.assertEqual(connection.recv(1), b"")
                    finally:
                        stopped.set()
                        sender.join(2)

    def test_one_account_cannot_occupy_all_request_slots(self):
        connections = []
        try:
            for _ in range(4):
                connection = socket.create_connection(self.server.server_address)
                connections.append(connection)
                connection.sendall(b"POST /v1/sync HTTP/1.1\r\nHost: local\r\nAuthorization: Bearer alpha\r\nContent-Length: 100\r\n\r\n{")
                self.assertTrue(self.ready.get(timeout=2))
            self.assertEqual(self.request("/v1/info")[0], 429)
            self.assertEqual(self.request("/v1/info", token="beta")[0], 200)
        finally:
            for connection in connections:
                connection.close()


class ProvisionFailureTests(unittest.TestCase):
    def test_missing_registry_directory_leaves_no_token(self):
        with tempfile.TemporaryDirectory() as root:
            token = Path(root) / "token"
            with self.assertRaises(OSError):
                provision.provision(Path(root) / "missing" / "accounts.json", token)
            self.assertFalse(token.exists())

    def test_partial_write_leaves_no_outputs(self):
        original = provision.os.fdopen

        class PartialWriter:
            def __init__(self, *args, **kwargs):
                self.output = original(*args, **kwargs)

            def __enter__(self):
                self.output.__enter__()
                return self

            def __exit__(self, *args):
                return self.output.__exit__(*args)

            def write(self, data):
                return self.output.write(data[:1])

        with tempfile.TemporaryDirectory() as root:
            registry, token = Path(root) / "accounts.json", Path(root) / "token"
            with patch.object(provision.os, "fdopen", side_effect=PartialWriter):
                with self.assertRaises(OSError):
                    provision.provision(registry, token)
            self.assertEqual(list(Path(root).iterdir()), [])

    def test_fsync_failure_leaves_no_outputs(self):
        for fail_at in range(1, 5):
            with self.subTest(fail_at=fail_at), tempfile.TemporaryDirectory() as root:
                registry, token = Path(root) / "registry" / "accounts.json", Path(root) / "secrets" / "token"
                registry.parent.mkdir()
                token.parent.mkdir()
                calls = 0
                original = provision.os.fsync

                def fail_once(descriptor):
                    nonlocal calls
                    calls += 1
                    if calls == fail_at:
                        raise OSError("injected fsync failure")
                    original(descriptor)

                with patch.object(provision.os, "fsync", side_effect=fail_once):
                    with self.assertRaises(OSError):
                        provision.provision(registry, token)
                self.assertFalse(registry.exists())
                self.assertFalse(token.exists())
                self.assertEqual(list(registry.parent.iterdir()), [])
                self.assertEqual(list(token.parent.iterdir()), [])

    def test_concurrent_provisioners_publish_only_one_pair(self):
        with tempfile.TemporaryDirectory() as root:
            registry = Path(root) / "accounts.json"
            outputs, errors = [], []
            barrier = threading.Barrier(2)

            def worker(index):
                barrier.wait()
                try:
                    provision.provision(registry, Path(root) / f"token-{index}")
                    outputs.append(index)
                except OSError as failure:
                    errors.append(failure)

            threads = [threading.Thread(target=worker, args=(index,)) for index in range(2)]
            for thread in threads:
                thread.start()
            for thread in threads:
                thread.join(5)
            self.assertEqual(len(outputs), 1)
            self.assertEqual(len(errors), 1)
            self.assertEqual(sorted(path.name for path in Path(root).iterdir()), ["accounts.json", f"token-{outputs[0]}"])
