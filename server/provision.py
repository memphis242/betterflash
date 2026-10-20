#!/usr/bin/env python3
"""Initialize a sync account registry and a private device access token file."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import secrets
from uuid import uuid4


def write_private(path, data):
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, "w", encoding="utf-8") as output:
        output.write(data)
        output.flush()
        os.fsync(output.fileno())
    directory = os.open(Path(path).parent, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(directory)
    finally:
        os.close(directory)


def provision(accounts_file, token_file):
    """Create one account without overwriting existing registries or secrets."""
    accounts_path, token_path = Path(accounts_file), Path(token_file)
    if accounts_path.absolute() == token_path.absolute():
        raise ValueError("Choose different paths for the account registry and token.")
    if accounts_path.exists() or accounts_path.is_symlink():
        raise FileExistsError("The account registry already exists. Edit its token digests to add or rotate credentials.")
    account_id = str(uuid4())
    token = secrets.token_urlsafe(32)
    registry = {"accounts": [{"id": account_id, "tokenSha256": [hashlib.sha256(token.encode()).hexdigest()]}]}
    write_private(token_path, token + "\n")
    write_private(accounts_path, json.dumps(registry, indent=2) + "\n")
    return account_id


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--accounts-file", required=True)
    parser.add_argument("--token-file", required=True)
    arguments = parser.parse_args()
    try:
        account_id = provision(arguments.accounts_file, arguments.token_file)
    except (OSError, ValueError) as failure:
        raise SystemExit(f"SYNC_PROVISION: {failure}. Existing files are preserved; check both output paths before retrying.") from None
    print(f"Account created: {account_id}")
    print("Enter the token from the private token file in each device's Sync settings.")


if __name__ == "__main__":
    main()
