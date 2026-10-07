#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
python3 "$root_dir/scripts/generate-brand-vectors.py" --root "$root_dir"
renderer="${SVG_RENDERER:-magick}"
if ! command -v "$renderer" >/dev/null 2>&1; then
  printf 'BRAND_RENDERER_MISSING: Install ImageMagick or set SVG_RENDERER to its executable: %s\n' "$renderer" >&2
  exit 1
fi

sizes=(16 20 22 24 32 40 48 64 96 128 192 256 512 1024)
render() {
  local source="$1"
  local destination="$2"
  local size="$3"
  mkdir -p "$(dirname "$destination")"
  "$renderer" -background none -density 300 "$source" -resize "${size}x${size}" \
    -strip -define png:color-type=6 -define png:bit-depth=8 "$destination"
}

for size in "${sizes[@]}"; do
  render "$root_dir/packaging/betterflash.svg" \
    "$root_dir/packaging/icons/${size}x${size}/betterflash.png" "$size"
  render "$root_dir/packaging/branding/recall-fold.svg" \
    "$root_dir/packaging/branding/png/dark/${size}.png" "$size"
  render "$root_dir/packaging/branding/recall-fold-light.svg" \
    "$root_dir/packaging/branding/png/light/${size}.png" "$size"
done

printf 'Exported %d sizes to packaging/icons and packaging/branding/png.\n' "${#sizes[@]}"
