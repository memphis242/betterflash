#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
master="${project_dir}/packaging/branding/walnut.png"

if ! command -v magick >/dev/null; then
    printf 'ICON_EXPORT_TOOL_MISSING: Install ImageMagick to export launcher icons.\n' >&2
    exit 1
fi
if [[ ! -f "$master" ]]; then
    printf 'ICON_MASTER_MISSING: Restore packaging/branding/walnut.png before exporting.\n' >&2
    exit 1
fi

export_icon() {
    local size="$1" destination="$2"
    mkdir -p "$(dirname "$destination")"
    magick "$master" -filter Lanczos -resize "${size}x${size}" -strip "$destination"
}

for size in 16 24 32 48 64 128 256 512; do
    export_icon "$size" "${project_dir}/packaging/icons/${size}x${size}/betterflash.png"
done
for entry in mdpi:48 hdpi:72 xhdpi:96 xxhdpi:144 xxxhdpi:192; do
    export_icon "${entry#*:}" "${project_dir}/android/res/mipmap-${entry%:*}/betterflash_icon.png"
done
printf 'Exported desktop and Android launcher icons from the colored walnut master.\n'
