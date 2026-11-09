#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
task_id="$(printf '%s' "$project_dir" | sha256sum | cut -c1-12)"
build_dir="${BETTERFLASH_BUILD_DIR:-/workspace/betterflash-build/${task_id}}"
cache_file="${build_dir}/CMakeCache.txt"
executable="${build_dir}/betterflash"

if [[ ! -f "$cache_file" ]]; then
    printf 'BUILD_CACHE_MISSING: Configure this worktree before installing its launcher.\n' >&2
    exit 1
fi
configured_source="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$cache_file")"
if [[ "$configured_source" != "$project_dir" ]]; then
    printf 'BUILD_SOURCE_MISMATCH: This build directory belongs to another checkout. Set BETTERFLASH_BUILD_DIR to a new directory.\n' >&2
    exit 1
fi
if [[ ! -x "$executable" ]]; then
    printf 'BUILD_EXECUTABLE_MISSING: Build %s before installing its launcher.\n' "$executable" >&2
    exit 1
fi

if [[ "$project_dir" == *$'\n'* || "$build_dir" == *$'\n'* ]]; then
    printf 'LAUNCHER_PATH_INVALID: Worktree and build paths cannot contain newlines.\n' >&2
    exit 1
fi

desktop_exec_arg() {
    local value="$1"
    local escaped=""
    local index char
    for ((index = 0; index < ${#value}; ++index)); do
        char="${value:index:1}"
        case "$char" in
            '%') escaped+='%%' ;;
            '\') escaped+='\\\\' ;;
            '"'|'`'|'$') escaped+='\\'; escaped+="$char" ;;
            *) escaped+="$char" ;;
        esac
    done
    printf '"%s"' "$escaped"
}

desktop_string() {
    local value="$1"
    value="${value//\\/\\\\}"
    value="${value//$'\t'/\\t}"
    value="${value//$'\r'/\\r}"
    printf '%s' "$value"
}

data_home="${XDG_DATA_HOME:-${HOME}/.local/share}"
applications_dir="${data_home}/applications"
desktop_file="${applications_dir}/org.betterflash.BetterFlash.desktop"
mkdir -p "$applications_dir"

source_marker="X-BetterFlash-Source=$(desktop_string "$project_dir")"
if [[ -L "$desktop_file" ]]; then
    printf 'LAUNCHER_EXISTS: Refusing to overwrite symlink %s.\n' "$desktop_file" >&2
    exit 1
fi
if [[ -e "$desktop_file" ]] && ! rg -F -x -- "$source_marker" "$desktop_file" >/dev/null; then
    printf 'LAUNCHER_EXISTS: Refusing to overwrite %s because it belongs to another source.\n' "$desktop_file" >&2
    exit 1
fi

install_root="${data_home}/betterflash/installed"
install_id="$(sha256sum "$executable" | cut -c1-16)"
release_dir="${install_root}/${install_id}"
install_log="${build_dir}/install-user.log"
mkdir -p "$install_root"
if [[ ! -x "${release_dir}/bin/betterflash" ]]; then
    staging_dir="$(mktemp -d "${install_root}/.install-XXXXXXXX")"
    if ! cmake --install "$build_dir" --prefix "$staging_dir" --strip >"$install_log" 2>&1; then
        printf 'INSTALL_FAILED: Could not install the built app into %s. Inspect %s before retrying. The existing launcher is retained.\n' "$staging_dir" "$install_log" >&2
        exit 1
    fi
    if [[ ! -x "${staging_dir}/bin/betterflash" ]]; then
        printf 'INSTALL_EXECUTABLE_MISSING: Installation did not produce %s/bin/betterflash. Inspect %s.\n' "$staging_dir" "$install_log" >&2
        exit 1
    fi
    if ! QT_QPA_PLATFORM=offscreen "${staging_dir}/bin/betterflash" --help >>"$install_log" 2>&1; then
        printf 'INSTALL_STARTUP_FAILED: The installed app could not start. Inspect %s. The existing launcher is retained.\n' "$install_log" >&2
        exit 1
    fi
    mv "$staging_dir" "$release_dir"
fi
env_command="$(command -v env)"
model_dir="${BETTERFLASH_MODEL_DIR:-}"
if [[ -z "$model_dir" && -d /workspace && -w /workspace ]]; then
    model_dir=/workspace/betterflash-models
fi
exec_line="$(desktop_exec_arg "${release_dir}/bin/betterflash")"
if [[ -n "$model_dir" ]]; then
    exec_line="$(desktop_exec_arg "$env_command") $(desktop_exec_arg "BETTERFLASH_MODEL_DIR=${model_dir}") ${exec_line}"
fi
icon_file="${release_dir}/share/icons/hicolor/512x512/apps/betterflash.png"
desktop_temporary="$(mktemp "${applications_dir}/.betterflash-XXXXXXXX.desktop")"
cat >"$desktop_temporary" <<EOF
[Desktop Entry]
Type=Application
Name=BetterFlash
Comment=Markdown flashcards with graded recall and voice review
Exec=${exec_line}
Icon=$(desktop_string "$icon_file")
Terminal=false
Categories=Education;
Keywords=flashcards;markdown;spaced repetition;study;
${source_marker}
EOF

if command -v desktop-file-validate >/dev/null 2>&1 && ! desktop-file-validate "$desktop_temporary"; then
    printf 'LAUNCHER_INVALID: desktop-file-validate rejected %s. The existing launcher is retained.\n' "$desktop_temporary" >&2
    exit 1
fi
chmod 644 "$desktop_temporary"
mv "$desktop_temporary" "$desktop_file"
if command -v update-desktop-database >/dev/null 2>&1; then
    if ! update-desktop-database "$applications_dir" >/dev/null 2>&1; then
        printf 'LAUNCHER_CACHE: Could not refresh the desktop application cache. Run update-desktop-database %s.\n' "$applications_dir" >&2
    fi
fi
printf 'User launcher installed: %s\n' "$desktop_file"
