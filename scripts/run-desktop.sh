#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
task_id="$(printf '%s' "$project_dir" | sha256sum | cut -c1-12)"
build_dir="${BETTERFLASH_BUILD_DIR:-/workspace/betterflash-build/${task_id}}"
if [[ ! -x "${build_dir}/betterflash" ]]; then
    "${project_dir}/scripts/build-desktop.sh"
fi
configured_source="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "${build_dir}/CMakeCache.txt")"
if [[ "$configured_source" != "$project_dir" ]]; then
    printf 'BUILD_SOURCE_MISMATCH: Rebuild from this worktree with a separate build directory.\n' >&2
    exit 1
fi
exec "${build_dir}/betterflash" "$@"

