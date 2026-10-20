import hashlib
import json
from pathlib import Path
import stat
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).parents[1]))
import provision


class ProvisionTests(unittest.TestCase):
    def test_private_files_contain_only_the_expected_credentials(self):
        with tempfile.TemporaryDirectory() as directory:
            accounts, token_path = Path(directory) / "accounts.json", Path(directory) / "token"
            account_id = provision.provision(accounts, token_path)
            token = token_path.read_text().strip()
            self.assertGreaterEqual(len(token), 32)
            self.assertEqual(json.loads(accounts.read_text()), {
                "accounts": [{"id": account_id, "tokenSha256": [hashlib.sha256(token.encode()).hexdigest()]}]})
            self.assertNotIn(token, accounts.read_text())
            for path in (accounts, token_path):
                self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)
            with self.assertRaises(FileExistsError):
                provision.provision(accounts, token_path)
            self.assertEqual(token_path.read_text().strip(), token)

    def test_existing_token_and_symlink_are_never_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            accounts, token_path = Path(directory) / "accounts.json", Path(directory) / "token"
            token_path.write_text("existing secret")
            with self.assertRaises(FileExistsError):
                provision.provision(accounts, token_path)
            self.assertFalse(accounts.exists())
            self.assertEqual(token_path.read_text(), "existing secret")
            accounts.symlink_to(Path(directory) / "missing")
            with self.assertRaises(FileExistsError):
                provision.provision(accounts, Path(directory) / "other-token")
