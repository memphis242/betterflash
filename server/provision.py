#!/usr/bin/env python3
"""Initialize a sync account registry and a private device access token file."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import secrets
import tempfile
from uuid import uuid4


def stage_private(path, data):
    """Create and sync a complete private file on the destination filesystem."""
    descriptor, temporary = tempfile.mkstemp(prefix=".betterflash-provision-", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as output:
            if output.write(data) != len(data):
                raise OSError("The staged credential file could not be written completely")
            output.flush()
            os.fsync(output.fileno())
        return Path(temporary)
    except BaseException:
        Path(temporary).unlink()
        raise


def sync_directory(path):
    descriptor = os.open(path, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def provision(accounts_file, token_file):
    """Publish complete files exclusively and roll back this invocation on failure."""
    accounts_path, token_path = Path(accounts_file).absolute(), Path(token_file).absolute()
    if accounts_path.resolve() == token_path.resolve():
        raise ValueError("Choose different paths for the account registry and token.")
    for path in (accounts_path, token_path):
        if path.exists() or path.is_symlink():
            raise FileExistsError("An output already exists. Existing registries and tokens are never overwritten.")
    account_id = str(uuid4())
    token = secrets.token_urlsafe(32)
    registry = {"accounts": [{"id": account_id, "tokenSha256": [hashlib.sha256(token.encode()).hexdigest()]}]}
    staged, published = [], []
    directories = tuple(dict.fromkeys((token_path.parent, accounts_path.parent)))
    try:
        for path, data in ((token_path, token + "\n"), (accounts_path, json.dumps(registry, indent=2) + "\n")):
            staged.append((path, stage_private(path, data)))
        for path, temporary in staged:
            # Hard links publish only complete files and cannot replace a competitor.
            metadata = temporary.stat()
            os.link(temporary, path)
            published.append((path, metadata.st_dev, metadata.st_ino))
        for _, temporary in staged:
            temporary.unlink()
        for directory in directories:
            sync_directory(directory)
        return account_id
    except BaseException as failure:
        for path, device, inode in reversed(published):
            try:
                metadata = path.lstat()
                if (metadata.st_dev, metadata.st_ino) == (device, inode):
                    path.unlink()
            except FileNotFoundError:
                pass
            except OSError as cleanup_failure:
                failure.add_note(f"Cannot roll back {path}: {cleanup_failure}. Check outputs before retrying.")
        for directory in directories:
            if directory.is_dir():
                try:
                    sync_directory(directory)
                except OSError as cleanup_failure:
                    failure.add_note(f"Cannot sync rollback directory {directory}: {cleanup_failure}.")
        raise
    finally:
        for _, temporary in staged:
            temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--accounts-file", required=True)
    parser.add_argument("--token-file", required=True)
    arguments = parser.parse_args()
    try:
        account_id = provision(arguments.accounts_file, arguments.token_file)
    except (OSError, ValueError) as failure:
        details = " ".join(getattr(failure, "__notes__", ()))
        raise SystemExit(f"SYNC_PROVISION: {failure}. {details} Existing files are preserved; check both output paths before retrying.") from None
    print(f"Account created: {account_id}")
    print("Enter the token from the private token file in each device's Sync settings.")


if __name__ == "__main__":
    main()
