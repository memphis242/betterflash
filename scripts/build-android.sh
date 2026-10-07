#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'HELP'
Usage: scripts/build-android.sh [--check] [--debug|--release]

Set QT_ANDROID_ROOT (Qt 6.11 arm64-v8a kit), QT_HOST_ROOT (matching host Qt),
ANDROID_SDK_ROOT (API 36 with Build Tools 36.0.0), ANDROID_NDK_ROOT (r27c),
JAVA_HOME (full JDK 21 or later), and ANDROID_OPENSSL_LIB_DIR (arm64 libraries
libcrypto_3.so and libssl_3.so). --check only reports missing prerequisites.

BETTERFLASH_ANDROID_BUILD_DIR selects a persistent build location under /workspace.
The default build is a debug APK. No device deployment or signing is performed.
HELP
}

android_check_only=false
android_build_type=Debug
for android_argument in "$@"; do
    case "$android_argument" in
        --check) android_check_only=true ;;
        --debug) android_build_type=Debug ;;
        --release) android_build_type=Release ;;
        --help|-h) usage; exit 0 ;;
        *) printf 'ANDROID_ARGUMENT_INVALID: Unknown argument %s. Use --help.\n' "$android_argument" >&2; exit 2 ;;
    esac
done

android_project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
android_path_digest="$(printf '%s' "$android_project_dir" | sha256sum)"
android_path_digest="${android_path_digest%% *}"
android_build_dir="${BETTERFLASH_ANDROID_BUILD_DIR:-/workspace/betterflash-build/android-${android_project_dir##*/}-${android_path_digest:0:12}}"

android_missing=0
missing() {
    printf '%s\n' "$1" >&2
    android_missing=$((android_missing + 1))
}

for android_tool in cmake ninja; do
    if ! command -v "$android_tool" >/dev/null 2>&1; then
        missing "ANDROID_BUILD_TOOL_MISSING: Install ${android_tool}."
    fi
done

if [[ -f "${android_build_dir}/CMakeCache.txt" ]]; then
    android_cached_source="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "${android_build_dir}/CMakeCache.txt")"
    if [[ "$android_cached_source" != "$android_project_dir" ]]; then
        missing "ANDROID_BUILD_DIRECTORY_IN_USE: ${android_build_dir} belongs to another source tree. Set BETTERFLASH_ANDROID_BUILD_DIR to a separate directory."
    fi
fi

if [[ -z "${QT_ANDROID_ROOT:-}" || ! -f "${QT_ANDROID_ROOT}/lib/cmake/Qt6/qt.toolchain.cmake" ]]; then
    missing 'ANDROID_QT_KIT_MISSING: Install the Qt 6.11 arm64-v8a Android kit and set QT_ANDROID_ROOT to its root.'
else
    for android_module in Core Concurrent Gui Quick QuickControls2 Svg Sql Network Multimedia TextToSpeech; do
        if [[ ! -f "${QT_ANDROID_ROOT}/lib/cmake/Qt6${android_module}/Qt6${android_module}Config.cmake" ]]; then
            missing "ANDROID_QT_MODULE_MISSING: Add Qt ${android_module} to the Android kit."
        fi
    done
fi

android_host_cmake=''
if [[ -n "${QT_HOST_ROOT:-}" ]]; then
    for android_lib_dir in lib lib64; do
        if [[ -f "${QT_HOST_ROOT}/${android_lib_dir}/cmake/Qt6/Qt6Config.cmake" ]]; then
            android_host_cmake="${QT_HOST_ROOT}/${android_lib_dir}/cmake/Qt6"
            break
        fi
    done
fi
if [[ -z "$android_host_cmake" ]]; then
    missing 'ANDROID_QT_HOST_MISSING: Set QT_HOST_ROOT to the matching desktop Qt installation with host development tools.'
elif [[ -n "${QT_ANDROID_ROOT:-}" && -f "${QT_ANDROID_ROOT}/lib/cmake/Qt6/Qt6ConfigVersionImpl.cmake" ]]; then
    android_kit_version="$(sed -n 's/^set(PACKAGE_VERSION "\([^"]*\)").*/\1/p' "${QT_ANDROID_ROOT}/lib/cmake/Qt6/Qt6ConfigVersionImpl.cmake")"
    android_host_version=''
    if [[ -f "${android_host_cmake}/Qt6ConfigVersionImpl.cmake" ]]; then
        android_host_version="$(sed -n 's/^set(PACKAGE_VERSION "\([^"]*\)").*/\1/p' "${android_host_cmake}/Qt6ConfigVersionImpl.cmake")"
    fi
    if [[ -z "$android_kit_version" || "$android_kit_version" != "$android_host_version" ]]; then
        missing "ANDROID_QT_VERSION_MISMATCH: Use matching host and Android Qt versions (host ${android_host_version:-unknown}, Android ${android_kit_version:-unknown})."
    fi
fi

if [[ -z "${JAVA_HOME:-}" || ! -x "${JAVA_HOME}/bin/java" || ! -x "${JAVA_HOME}/bin/javac" ]]; then
    missing 'ANDROID_JDK_MISSING: Install a full JDK 21 or later and set JAVA_HOME to it. A Java runtime alone is insufficient.'
else
    if ! android_javac_version="$("${JAVA_HOME}/bin/javac" -version 2>&1)"; then
        missing 'ANDROID_JDK_INVALID: javac failed to start. Select a working full JDK.'
    elif [[ "$android_javac_version" =~ javac[[:space:]]+([0-9]+) ]]; then
        if (( BASH_REMATCH[1] < 21 )); then
            missing 'ANDROID_JDK_TOO_OLD: Use JDK 21 or later.'
        fi
    else
        missing 'ANDROID_JDK_INVALID: Could not read javac version. Select a working full JDK.'
    fi
fi

if [[ -z "${ANDROID_SDK_ROOT:-}" || ! -d "${ANDROID_SDK_ROOT}" ]]; then
    missing 'ANDROID_SDK_MISSING: Install Android command line tools, SDK platform 36, Build Tools 36.0.0, and platform tools; set ANDROID_SDK_ROOT.'
else
    if [[ ! -f "${ANDROID_SDK_ROOT}/platforms/android-36/android.jar" ]]; then
        missing 'ANDROID_SDK_PLATFORM_MISSING: Install platforms;android-36 with sdkmanager.'
    fi
    if [[ ! -x "${ANDROID_SDK_ROOT}/build-tools/36.0.0/aapt2" ]]; then
        missing 'ANDROID_BUILD_TOOLS_MISSING: Install build-tools;36.0.0 with sdkmanager.'
    fi
    if [[ ! -x "${ANDROID_SDK_ROOT}/platform-tools/adb" ]]; then
        missing 'ANDROID_PLATFORM_TOOLS_MISSING: Install platform-tools with sdkmanager.'
    fi
fi

if [[ -z "${ANDROID_NDK_ROOT:-}" || ! -f "${ANDROID_NDK_ROOT}/source.properties" ]]; then
    missing 'ANDROID_NDK_MISSING: Install ndk;27.2.12479018 and set ANDROID_NDK_ROOT to that NDK root.'
else
    android_ndk_version="$(sed -n 's/^Pkg.Revision[[:space:]]*=[[:space:]]*//p' "${ANDROID_NDK_ROOT}/source.properties")"
    if [[ "$android_ndk_version" != '27.2.12479018' ]]; then
        missing "ANDROID_NDK_VERSION_MISMATCH: Qt 6.11 requires NDK 27.2.12479018; selected ${android_ndk_version:-unknown}."
    fi
    if [[ ! -x "${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++" ]]; then
        missing 'ANDROID_NDK_COMPILER_MISSING: Reinstall the NDK Linux toolchain.'
    fi
fi

if [[ -z "${ANDROID_OPENSSL_LIB_DIR:-}" || ! -f "${ANDROID_OPENSSL_LIB_DIR}/libcrypto_3.so" || ! -f "${ANDROID_OPENSSL_LIB_DIR}/libssl_3.so" ]]; then
    missing 'ANDROID_OPENSSL_MISSING: Set ANDROID_OPENSSL_LIB_DIR to arm64-v8a libcrypto_3.so and libssl_3.so for HTTPS. See docs/android.md.'
fi

if (( android_missing > 0 )); then
    printf 'ANDROID_ENVIRONMENT_INCOMPLETE: %d prerequisites need attention. See docs/android.md.\n' "$android_missing" >&2
    exit 2
fi

if "$android_check_only"; then
    printf 'Android build prerequisites are ready. No build was started.\n'
    exit 0
fi

cmake -S "$android_project_dir" -B "$android_build_dir" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="${QT_ANDROID_ROOT}/lib/cmake/Qt6/qt.toolchain.cmake" \
    -DQT_HOST_PATH="${QT_HOST_ROOT}" \
    -DANDROID_SDK_ROOT="${ANDROID_SDK_ROOT}" -DANDROID_NDK="${ANDROID_NDK_ROOT}" \
    -DANDROID_PLATFORM=android-28 -DANDROID_ABI=arm64-v8a \
    -DBETTERFLASH_ANDROID_OPENSSL_LIB_DIR="${ANDROID_OPENSSL_LIB_DIR}" \
    -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE="$android_build_type"
cmake --build "$android_build_dir" --target apk --parallel "$(getconf _NPROCESSORS_ONLN)"
printf 'Android package build finished. APK output directory: %s/android-build/build/outputs/apk\n' "$android_build_dir"
