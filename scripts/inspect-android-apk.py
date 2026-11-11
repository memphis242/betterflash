#!/usr/bin/env python3
"""Inspect arm64 APK native libraries and their 16 KiB ELF load alignment."""

import argparse
import struct
import sys
import zipfile
from pathlib import Path


def inspect_library(name: str, data: bytes) -> set[str]:
    if len(data) < 64 or data[:6] != b"\x7fELF\x02\x01":
        raise ValueError(f"{name}: expected a little-endian ELF64 library")
    machine = struct.unpack_from("<H", data, 18)[0]
    if machine != 183:
        raise ValueError(f"{name}: expected AArch64 machine 183, observed {machine}")
    offset = struct.unpack_from("<Q", data, 32)[0]
    entry_size, count = struct.unpack_from("<HH", data, 54)
    if entry_size < 56 or not count or offset + entry_size * count > len(data):
        raise ValueError(f"{name}: invalid ELF program header table")
    loads = 0
    segments = []
    dynamic = None
    for index in range(count):
        fields = struct.unpack_from("<IIQQQQQQ", data, offset + index * entry_size)
        kind, _, file_offset, address, _, _, _, alignment = fields
        if kind == 2:
            dynamic = (file_offset, fields[5])
        if kind != 1:
            continue
        if fields[5] > fields[6] or file_offset + fields[5] > len(data):
            raise ValueError(f"{name}: LOAD {index} extends beyond the ELF file or its memory segment")
        segments.append((address, file_offset, fields[5]))
        loads += 1
        if alignment < 16384 or alignment & (alignment - 1) or (address - file_offset) % 16384:
            raise ValueError(
                f"{name}: LOAD {index} has alignment {alignment:#x}, offset {file_offset:#x}, "
                f"address {address:#x}; rebuild this dependency for 16 KiB pages"
            )
    if not loads:
        raise ValueError(f"{name}: no ELF LOAD segments")
    if dynamic is None:
        return set()
    dynamic_offset, dynamic_size = dynamic
    if dynamic_offset + dynamic_size > len(data) or dynamic_size % 16:
        raise ValueError(f"{name}: invalid ELF dynamic table")
    needed = []
    strings_address = None
    strings_size = 0
    for position in range(dynamic_offset, dynamic_offset + dynamic_size, 16):
        tag, value = struct.unpack_from("<qQ", data, position)
        if tag == 0:
            break
        if tag == 1:
            needed.append(value)
        elif tag == 5:
            strings_address = value
        elif tag == 10:
            strings_size = value
    if not needed:
        return set()
    for address, file_offset, size in segments:
        if strings_address is not None and address <= strings_address < address + size:
            strings_offset = file_offset + strings_address - address
            if strings_address + strings_size > address + size or strings_offset + strings_size > len(data):
                break
            strings = data[strings_offset:strings_offset + strings_size]
            result = set()
            for index in needed:
                end = strings.find(b"\0", index)
                if index >= strings_size or end < 0:
                    raise ValueError(f"{name}: invalid ELF dependency name offset {index}")
                result.add(strings[index:end].decode("ascii"))
            return result
    raise ValueError(f"{name}: cannot locate the ELF dependency string table")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("apk", type=Path)
    parser.add_argument("--system-libs-dir", type=Path,
                        help="NDK sysroot library directory for the minimum Android API")
    args = parser.parse_args()
    try:
        with zipfile.ZipFile(args.apk) as package:
            libraries = [entry for entry in package.infolist()
                         if entry.filename.startswith("lib/") and entry.filename.endswith(".so")]
            if not libraries:
                raise ValueError("APK contains no native libraries")
            names = set()
            dependencies = {}
            failures = []
            for entry in libraries:
                try:
                    if entry.filename in names:
                        raise ValueError(f"{entry.filename}: duplicate native library entry")
                    names.add(entry.filename)
                    if not entry.filename.startswith("lib/arm64-v8a/"):
                        raise ValueError(f"{entry.filename}: expected the arm64-v8a ABI only")
                    if entry.file_size > 512 * 1024 * 1024:
                        raise ValueError(f"{entry.filename}: exceeds the 512 MiB inspection limit")
                    dependencies[entry.filename] = inspect_library(entry.filename, package.read(entry))
                except ValueError as error:
                    failures.append(str(error))
            if args.system_libs_dir is not None:
                system_libraries = {path.name for path in args.system_libs_dir.glob("*.so")}
                if not system_libraries:
                    raise ValueError(f"no Android system library stubs in {args.system_libs_dir}; select the NDK minimum-API directory")
                available = system_libraries | {Path(name).name for name in names}
                for name, needed in dependencies.items():
                    for missing in sorted(needed - available):
                        failures.append(f"{name}: missing native dependency {missing}; bundle the matching arm64 runtime")
            if failures:
                raise ValueError("\n".join(failures))
            print(f"APK_NATIVE_READY: {args.apk}: {len(libraries)} arm64 libraries support 16 KiB ELF pages.")
            print("Device launch and runtime behavior still require physical-device verification.")
    except (OSError, ValueError, zipfile.BadZipFile, RuntimeError, struct.error) as error:
        print(f"APK_INSPECTION_FAILED: {args.apk}: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
