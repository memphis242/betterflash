# Android development

For the next-session handoff, target device, researched setup and phased work,
start with [Android implementation plan](android-plan.md). This document describes
the Qt 6.12/API 36 implementation. Android 17/API 37 runtime compatibility
and physical-device behavior still require verification.

The Android target shares the desktop C++ data, scheduling, Markdown, network, and voice modules with the QML interface. Its minimum Android version is API 28 and target version is API 36. The custom manifest retains the Qt Application, Activity, native library placeholders, configuration handling, and FileProvider needed for a Qt package. It declares network and microphone access, treats a microphone as optional, and includes the visibility query for system speech engines.

Microphone permission is requested through `QCoreApplication::requestPermission(QMicrophonePermission{}, ...)` only when voice is enabled. Declaring the permission never starts recording. Voice is disabled when the Android application leaves the foreground. Foreground review is the initial scope; locked-screen or background review still needs a microphone foreground service, notification controls, audio focus handling, Bluetooth route testing, and lifecycle recovery. Key entry currently provides session storage on Android; encrypted persistence using Android Keystore remains a draft task.

The desktop Qt development packages do not include an Android Qt kit. The build preflight reports missing pieces before doing any compilation:

```sh
./scripts/build-android.sh --check
```

Install a Qt 6.12 arm64 Android kit with Core, Concurrent, GUI, Quick, Quick Controls, SQL, Network, Multimedia, and Text to Speech modules, plus the matching desktop Qt host tools. Use a full JDK 21 or later and set `JAVA_HOME`; a Java runtime alone cannot compile Android code. The checked SDK components are platform 36, Build Tools 36.0.0, platform tools, and NDK r27c `27.2.12479018`. [Qt 6.12 supported configurations](https://doc.qt.io/qt-6.12/android.html), [Qt Android prerequisites](https://doc.qt.io/qt-6.12/android-configure-dev-environment.html)

With Android command line tools installed, the SDK packages are:

```sh
sdkmanager "platform-tools" "platforms;android-36" "build-tools;36.0.0" "ndk;27.2.12479018"
```

For Groq and HTTPS sync, package arm64-v8a OpenSSL libraries named `libcrypto_3.so` and `libssl_3.so`. Qt Creator's Android setup can download the supported libraries, or they can be built using Qt's documented procedure. Qt's Android kit alone does not bundle them. [Qt Android OpenSSL setup](https://doc.qt.io/qt-6.12/android-openssl-support.html)

Set paths to the installed tools and libraries, then run:

```sh
export QT_ANDROID_ROOT=/path/to/Qt/6.12.0/android_arm64_v8a
export QT_HOST_ROOT=/path/to/Qt/6.12.0/gcc_64
export ANDROID_SDK_ROOT=/path/to/Android/Sdk
export ANDROID_HOME="$ANDROID_SDK_ROOT"
export ANDROID_NDK_ROOT="$ANDROID_SDK_ROOT/ndk/27.2.12479018"
export JAVA_HOME=/path/to/full/jdk
export ANDROID_OPENSSL_LIB_DIR=/path/to/openssl/arm64-v8a
./scripts/build-desktop.sh
./scripts/build-android.sh --check
./scripts/build-android.sh --debug
```

The script derives its persistent build directory under `/workspace/betterflash-build` from the complete worktree path and a SHA-256 path prefix, sets native API 28, and requests Qt's `apk` target with parallel compilation. `BETTERFLASH_ANDROID_BUILD_DIR` overrides the build location; an existing CMake cache must point at this worktree. `--release` prepares a release build; device deployment and signing are separate actions. With Fedora's system host Qt, `QT_HOST_ROOT=/usr` is accepted when its version matches the Android kit.

Code highlighting shares the native lexer and themes with desktop; external
language servers are disabled on Android. Its build needs the desktop build's
`bin/katehighlightingindexer` to generate bundled language data. The script finds
this tool in the same worktree's desktop build, honoring `BETTERFLASH_BUILD_DIR`.
Set `KATEHIGHLIGHTINGINDEXER_EXECUTABLE` to an absolute path to override it.

The installed environment is Qt 6.12.0 at `/workspace/Qt`, JDK 25 at
`/usr/lib/jvm/java-25-openjdk`, and SDK at `/workspace/Android/Sdk`. The NDK is
`ndk/27.2.12479018` and OpenSSL is `android_openssl/ssl_3/arm64-v8a` under the SDK.
SDK 37 is also installed; this build explicitly compiles and targets API 36.
Physical-device startup, permissions, audio, lifecycle and two-device sync remain
acceptance gates.

On 2026-11-10, the arm64 debug APK built successfully with this toolchain. Its
103 packaged native libraries passed ELF 16 KiB alignment and native dependency
inspection against the API 28 system stubs; APK ZIP alignment also passed.
The desktop target compiled and isolated 412 x 915 previews rendered the review
overview and deck library without QML warnings. No automated tests or physical
device checks were run. These results do not establish Android 17 runtime support.

Image import currently accepts local file URLs. Android document-provider
`content:` URLs still need a read adapter before phone attachment import is
complete.

## Semantic search draft

Automatic model selection uses BGE small English v1.5 on Android, with a
roughly 34 MB quantized model download, rather than the desktop's roughly
110 MB BGE base model. Both choices remain available in Search settings.
The CMake configuration fetches the pinned ONNX Runtime Android 1.20.0 AAR,
extracts its native headers and matching ABI library, and appends that library
to the package alongside OpenSSL. CPU inference uses two threads for the
compact profile. No Python service is required on the phone.

Model download, lifecycle cancellation, memory use and retrieval latency need
physical-device verification. Desktop builds and runtime
launches do not establish Android readiness.

The Android language-model default is the compact LFM 2.5 350M profile. The
larger Gemma profile is reserved for desktop hardware. Android local speech is
still a draft because the current managed Vosk and Piper packages target Linux
x86-64; compatible Android-native packages, lifecycle handling, and device
validation are required before offline voice can be enabled there.

## Phone navigation and packaging

At widths up to 600 logical pixels, pinned bottom tabs replace the desktop
navigation rail. Library shows either decks or cards at full width, with a Decks
return action. Phone pane changes retain the desktop split-pane preferences.
Android Back dismisses the keyboard, returns Cards to Decks and other pages to
Review, and pauses active review before allowing exit. Card/deck editor drafts
require their explicit Save/Cancel controls. Toolbars include safe-area padding;
actual Samsung system bars, rotation and the soft keyboard still need checking.

The build uses the CPU language-model backend as its initial Android baseline.
Vulkan acceleration is not enabled by the Android build script. Libarchive 3.8.1
is compiled statically for Android with NDK zlib for ZIP/gzip/tar model assets;
bzip2 model archives are not supported on Android. Native code is linked with
NDK flexible page-size support. After packaging, the build runs
`scripts/inspect-android-apk.py` over every packaged native library and checks APK
alignment using `zipalign -c -P 16 4`. These checks establish packaging properties,
not successful runtime operation or model quality.

`android/build.gradle` pins Android Gradle Plugin 9.2.1, matching Qt 6.12.0,
and applies the installed kit's template for its source sets and defaults. It
preserves the packaged `libavformat.so` because NDK r27's `llvm-strip` moves a
NOTE segment in this prebuilt without preserving its LOAD offset congruence.
The package inspector checks that congruence as well as segment alignment. The
NDK arm64 OpenMP runtime is explicitly bundled for the CPU model backend.

Once USB debugging is authorized, select the phone explicitly (especially if
an emulator is running):

```sh
ADB=/workspace/Android/Sdk/platform-tools/adb
"$ADB" devices -l
# Replace SERIAL and /path/to/betterflash.apk with the selected device and build.
"$ADB" -s SERIAL shell getprop ro.build.version.sdk
"$ADB" -s SERIAL shell getprop ro.product.cpu.abilist
"$ADB" -s SERIAL shell getconf PAGE_SIZE
"$ADB" -s SERIAL install -r /path/to/betterflash.apk
"$ADB" -s SERIAL shell am start -W -n org.betterflash.app/org.qtproject.qt.android.bindings.QtActivity
"$ADB" -s SERIAL shell pidof org.betterflash.app
# Use the returned PID for app-only runtime logs.
"$ADB" -s SERIAL logcat --pid=PID -d
```

A debug APK uses the development signing key. Keep it for in-place updates;
changing the signing key requires an explicit migration or uninstall, which
removes app-private data. Never uninstall automatically to bypass an install
error. `--release` does not configure a distribution signing key.
