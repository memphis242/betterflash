#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
task_id="$(printf '%s' "$project_dir" | sha256sum | cut -c1-12)"
build_dir="${BETTERFLASH_BUILD_DIR:-/workspace/betterflash-build/${task_id}}"
"${project_dir}/scripts/build-desktop.sh" -DBUILD_TESTING=ON
QT_LOGGING_RULES='*.warning=true;*.critical=true' QT_FORCE_STDERR_LOGGING=1 \
    ctest --test-dir "$build_dir" --output-on-failure --parallel "${BETTERFLASH_JOBS:-24}"
printf 'GUI reports and captures: %s/artifacts/gui\n' "$build_dir"
