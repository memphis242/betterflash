#!/usr/bin/env python3
"""Generate derived BetterFlash branding vectors from the canonical sources."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from xml.etree import ElementTree


INK = "#322832"
IVORY = "#f7eee8"
BACKGROUND = "#211722"


def path_from_svg(path: Path, index: int = 0) -> str:
    root = ElementTree.parse(path).getroot()
    paths = root.findall("{http://www.w3.org/2000/svg}path")
    if index >= len(paths):
        raise ValueError(f"{path} does not contain path {index}")
    return paths[index].attrib["d"]


def write(path: Path, content: str) -> None:
    path.write_text(content + "\n", encoding="utf-8")


def mark_svg(path_data: str, color: str) -> str:
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="512" height="512" viewBox="0 0 512 512">
  <title>BetterFlash Recall fold mark</title>
  <path fill="{color}" fill-rule="evenodd" d="{path_data}"/>
</svg>'''


def launcher_svg(path_data: str) -> str:
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="512" height="512" viewBox="0 0 512 512">
  <title>BetterFlash launcher icon</title>
  <rect width="512" height="512" rx="96" fill="{BACKGROUND}"/>
  <path fill="{IVORY}" fill-rule="evenodd" d="{path_data}"/>
</svg>'''


def lockup_svg(mark: str, wordmark: str, color: str) -> str:
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="768" height="768" viewBox="0 0 768 768">
  <title>BetterFlash lockup</title>
  <path fill="{color}" fill-rule="evenodd" d="{mark}" transform="translate(128 32)"/>
  <path fill="{color}" d="{wordmark}" transform="translate(149 620)"/>
</svg>'''


def android_vector(mark: str) -> str:
    rounded = "M96,0 C42.98,0 0,42.98 0,96 L0,416 C0,469.02 42.98,512 96,512 L416,512 C469.02,512 512,469.02 512,416 L512,96 C512,42.98 469.02,0 416,0 Z"
    return f'''<?xml version="1.0" encoding="utf-8"?>
<vector xmlns:android="http://schemas.android.com/apk/res/android"
    android:width="64dp" android:height="64dp" android:viewportWidth="512" android:viewportHeight="512">
    <path android:fillColor="@color/betterflash_launcher_background" android:pathData="{rounded}"/>
    <path android:fillColor="@color/betterflash_icon_ink" android:fillType="evenOdd" android:pathData="{mark}"/>
</vector>'''


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    root = args.root.resolve()
    branding = root / "packaging" / "branding"
    canonical = branding / "recall-fold.svg"
    wordmark_source = branding / "wordmark.svg"
    mark = path_from_svg(canonical)
    wordmark = path_from_svg(wordmark_source)
    write(branding / "recall-fold-light.svg", mark_svg(mark, INK))
    write(root / "packaging" / "betterflash.svg", launcher_svg(mark))
    write(branding / "betterflash-lockup.svg", lockup_svg(mark, wordmark, IVORY))
    write(branding / "betterflash-lockup-light.svg", lockup_svg(mark, wordmark, INK))
    write(root / "android" / "res" / "drawable" / "betterflash_icon.xml", android_vector(mark))
    write(root / "android" / "res" / "values" / "colors.xml", f'''<?xml version="1.0" encoding="utf-8"?>
<resources>
    <color name="betterflash_icon_ink">{IVORY}</color>
    <color name="betterflash_launcher_background">{BACKGROUND}</color>
</resources>''')


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, ElementTree.ParseError) as error:
        sys.exit(f"BRAND_EXPORT_FAILED: {error}. Check the canonical SVG sources and output permissions.")
