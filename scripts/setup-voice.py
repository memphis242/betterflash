#!/usr/bin/env python3
"""Download explicitly requested local Vosk test assets without a pip environment."""
from __future__ import annotations

import argparse
import hashlib
import json
import platform
import shutil
import stat
import sys
import tempfile
import urllib.request
import zipfile
from pathlib import Path, PurePosixPath
from urllib.parse import urlparse

MODEL_PROFILES = {
    "balanced-en": {
        "name": "vosk-model-en-us-0.22-lgraph",
        "bytes": 130557655,
        "sha256": "d9838b4aaa82a75c4a17f5aca300eaca129aaab2a7cbf951bafbb500eb9c4334",
        "destination": "model-balanced-en",
    },
    "small-en": {
        "name": "vosk-model-small-en-us-0.15",
        "bytes": 41205931,
        "sha256": "30f26242c4eb449f948e42cb302dd7a686cb29a3423a8367f99ff41780942498",
        "destination": "model",
    },
}
VOSK_VERSION = "0.3.45"
MAX_DOWNLOAD_BYTES = 128 * 1024 * 1024
MAX_EXTRACTED_BYTES = 512 * 1024 * 1024


def download(url: str, destination: Path, maximum: int = MAX_DOWNLOAD_BYTES) -> str:
    if urlparse(url).scheme != "https":
        raise ValueError("VOICE_DOWNLOAD_URL_INVALID: Asset URLs must use HTTPS.")
    request = urllib.request.Request(url, headers={"User-Agent": "BetterFlash-voice-setup/1"})
    digest = hashlib.sha256()
    total = 0
    with urllib.request.urlopen(request, timeout=60) as response, destination.open("wb") as output:
        if urlparse(response.geturl()).scheme != "https":
            raise ValueError("VOICE_DOWNLOAD_REDIRECT_INVALID: An asset redirected away from HTTPS.")
        while chunk := response.read(65536):
            total += len(chunk)
            if total > maximum:
                raise ValueError("VOICE_DOWNLOAD_TOO_LARGE: Asset exceeds the download limit.")
            digest.update(chunk)
            output.write(chunk)
    return digest.hexdigest()


def safe_member(info: zipfile.ZipInfo) -> PurePosixPath:
    name = PurePosixPath(info.filename)
    if name.is_absolute() or ".." in name.parts or "\\" in info.filename or stat.S_ISLNK(info.external_attr >> 16):
        raise ValueError("VOICE_ARCHIVE_INVALID: Asset contains an unsafe archive path.")
    return name


def extract_model(archive: Path, destination: Path, model_name: str) -> None:
    with zipfile.ZipFile(archive) as source:
        members = source.infolist()
        if len(members) > 4096 or sum(info.file_size for info in members) > MAX_EXTRACTED_BYTES:
            raise ValueError("VOICE_ARCHIVE_TOO_LARGE: Extracted model exceeds the asset limit.")
        for info in members:
            name = safe_member(info)
            if not name.parts or name.parts[0] != model_name:
                raise ValueError("VOICE_ARCHIVE_INVALID: Model archive has an unexpected root directory.")
            if info.is_dir():
                continue
            relative = Path(*name.parts[1:])
            if not relative.parts:
                raise ValueError("VOICE_ARCHIVE_INVALID: Model file has an empty path.")
            target = destination / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            with source.open(info) as input_file, target.open("wb") as output_file:
                shutil.copyfileobj(input_file, output_file, length=65536)
    if not (destination / "am/final.mdl").is_file() or not (destination / "conf/model.conf").is_file():
        raise ValueError("VOICE_MODEL_INCOMPLETE: Downloaded archive lacks the Vosk model files.")


def run(args: argparse.Namespace) -> None:
    machine = platform.machine().lower()
    architectures = {"x86_64": "x86_64", "aarch64": "aarch64", "arm64": "aarch64"}
    if sys.platform != "linux" or machine not in architectures:
        raise ValueError("VOICE_PLATFORM_UNSUPPORTED: Local test setup supports 64-bit x86 or ARM Linux.")
    target = args.directory.expanduser().resolve()
    profile = MODEL_PROFILES[args.profile]
    model_name = profile["name"]
    model_url = f"https://alphacephei.com/vosk/models/{model_name}.zip"
    model_destination = target / profile["destination"]
    if model_destination.exists():
        raise ValueError("VOICE_ASSETS_EXIST: Choose another --directory; installed assets are preserved.")
    target.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".install-", dir=target) as temporary:
        staging = Path(temporary)
        metadata = staging / "pypi.json"
        download(f"https://pypi.org/pypi/vosk/{VOSK_VERSION}/json", metadata, maximum=1024 * 1024)
        releases = json.loads(metadata.read_text())["urls"]
        if not isinstance(releases, list) or any(not isinstance(release, dict) or not isinstance(release.get("filename"), str) for release in releases):
            raise ValueError("VOICE_LIBRARY_METADATA_INVALID: PyPI returned unreadable release metadata.")
        architecture = architectures[machine]
        choices = [release for release in releases if release["filename"].endswith(f"_{architecture}.whl")
                   and "manylinux" in release["filename"]]
        if len(choices) != 1:
            raise ValueError("VOICE_LIBRARY_UNAVAILABLE: PyPI has no unique Vosk wheel for this Linux architecture.")
        release = choices[0]
        if urlparse(release["url"]).hostname != "files.pythonhosted.org":
            raise ValueError("VOICE_LIBRARY_URL_INVALID: PyPI returned an unexpected artifact host.")
        wheel = staging / "vosk.whl"
        wheel_digest = download(release["url"], wheel)
        if wheel_digest != release["digests"]["sha256"]:
            raise ValueError("VOICE_LIBRARY_DIGEST_INVALID: Vosk wheel checksum did not match PyPI metadata.")
        with zipfile.ZipFile(wheel) as source:
            libraries = [info for info in source.infolist() if info.filename == "vosk/libvosk.so"]
            if len(libraries) != 1 or libraries[0].file_size > MAX_DOWNLOAD_BYTES:
                raise ValueError("VOICE_LIBRARY_INVALID: Vosk wheel lacks the expected shared library.")
            safe_member(libraries[0])
            with source.open(libraries[0]) as input_file, (staging / "libvosk.so").open("wb") as output_file:
                shutil.copyfileobj(input_file, output_file, length=65536)
        model_zip = staging / "model.zip"
        model_digest = download(model_url, model_zip)
        if model_zip.stat().st_size != profile["bytes"] or model_digest != profile["sha256"]:
            raise ValueError("VOICE_MODEL_DIGEST_INVALID: Model checksum or byte size did not match the pinned release.")
        if args.model_sha256 and model_digest != args.model_sha256.lower():
            raise ValueError("VOICE_MODEL_DIGEST_INVALID: Model checksum did not match --model-sha256.")
        extract_model(model_zip, staging / profile["destination"], model_name)
        provenance = {"model_profile": args.profile, "model_name": model_name,
                      "model_url": model_url, "model_bytes": profile["bytes"],
                      "model_sha256": model_digest,
                      "library_url": release["url"], "library_sha256": wheel_digest, "version": VOSK_VERSION}
        selected_model = staging / profile["destination"]
        (selected_model / "libvosk.so").write_bytes((staging / "libvosk.so").read_bytes())
        (selected_model / "betterflash-assets.json").write_text(json.dumps(provenance, indent=2) + "\n")
        selected_model.rename(model_destination)
    print(f"Local test model and library installed. Set Voice > Model path to {model_destination}")
    print(f"Model SHA-256: {model_digest}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, default=Path("/workspace/betterflash-models/voice"))
    parser.add_argument("--vosk", action="store_true", help="explicitly download the optional local model and native library")
    parser.add_argument("--profile", choices=sorted(MODEL_PROFILES), default="balanced-en",
                        help="local Vosk profile to install (default: balanced-en)")
    parser.add_argument("--model-sha256", default="", help="additional SHA-256 check for the selected model")
    args = parser.parse_args()
    if not args.vosk:
        parser.error("pass --vosk to request local test assets; Groq needs no local model")
    if args.model_sha256 and (len(args.model_sha256) != 64 or any(c not in "0123456789abcdefABCDEF" for c in args.model_sha256)):
        parser.error("--model-sha256 must contain 64 hexadecimal characters")
    try:
        run(args)
    except (OSError, ValueError, KeyError, TypeError, RuntimeError, zipfile.BadZipFile) as error:
        print(f"VOICE_SETUP_FAILED: {error} Check the connection and choose a writable asset directory.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
