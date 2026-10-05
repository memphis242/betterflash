#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
task_id="$(printf '%s' "$project_dir" | sha256sum | cut -c1-12)"
build_dir="${BETTERFLASH_BUILD_DIR:-/workspace/betterflash-build/${task_id}}"

for program in cmake ninja c++; do
    if ! command -v "$program" >/dev/null; then
        printf 'BUILD_TOOL_MISSING: Install %s. See the Fedora command in README.md.\n' "$program" >&2
        exit 1
    fi
done
if [[ -f "${build_dir}/CMakeCache.txt" ]]; then
    configured_source="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "${build_dir}/CMakeCache.txt")"
    if [[ "$configured_source" != "$project_dir" ]]; then
        printf 'BUILD_SOURCE_MISMATCH: This build directory belongs to another checkout. Set BETTERFLASH_BUILD_DIR to a new directory.\n' >&2
        exit 1
    fi
fi
cmake -S "$project_dir" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON "$@"
cmake --build "$build_dir" --parallel "${BETTERFLASH_JOBS:-24}"
printf 'Desktop executable: %s/betterflash\n' "$build_dir"

