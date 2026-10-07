# Android draft

The Android target shares the desktop C++ data, scheduling, Markdown, network, and voice modules with the QML interface. Its minimum Android version is API 28 and target version is API 36. The custom manifest retains the Qt Application, Activity, native library placeholders, configuration handling, and FileProvider needed for a Qt package. It declares network and microphone access, treats a microphone as optional, and includes the visibility query for system speech engines.

Microphone permission is requested through `QCoreApplication::requestPermission(QMicrophonePermission{}, ...)` only when voice is enabled. Declaring the permission never starts recording. Voice is disabled when the Android application leaves the foreground. Foreground review is the initial scope; locked-screen or background review still needs a microphone foreground service, notification controls, audio focus handling, Bluetooth route testing, and lifecycle recovery. Key entry currently provides session storage on Android; encrypted persistence using Android Keystore remains a draft task.

The desktop Qt development packages do not include an Android Qt kit. The build preflight reports missing pieces before doing any compilation:

```sh
./scripts/build-android.sh --check
```

Install a Qt 6.11 arm64 Android kit with Core, Concurrent, GUI, Quick, Quick Controls, SQL, Network, Multimedia, and Text to Speech modules, plus the matching desktop Qt host tools. Use a full JDK 21 or later and set `JAVA_HOME`; a Java runtime alone cannot compile Android code. The checked SDK components are platform 36, Build Tools 36.0.0, platform tools, and NDK r27c `27.2.12479018`. [Qt 6.11 supported configurations](https://doc.qt.io/qt-6.11/android.html), [Qt Android prerequisites](https://doc.qt.io/qt-6.11/android-configure-dev-environment.html)

With Android command line tools installed, the SDK packages are:

```sh
sdkmanager "platform-tools" "platforms;android-36" "build-tools;36.0.0" "ndk;27.2.12479018"
```

For Groq and HTTPS sync, package arm64-v8a OpenSSL libraries named `libcrypto_3.so` and `libssl_3.so`. Qt Creator's Android setup can download the supported libraries, or they can be built using Qt's documented procedure. Qt's Android kit alone does not bundle them. [Qt Android OpenSSL setup](https://doc.qt.io/qt-6.11/android-openssl-support.html)

Set paths to the installed tools and libraries, then run:

```sh
export QT_ANDROID_ROOT=/path/to/Qt/6.11.2/android_arm64_v8a
export QT_HOST_ROOT=/path/to/Qt/6.11.2/gcc_64
export ANDROID_SDK_ROOT=/path/to/Android/Sdk
export ANDROID_NDK_ROOT="$ANDROID_SDK_ROOT/ndk/27.2.12479018"
export JAVA_HOME=/path/to/full/jdk
export ANDROID_OPENSSL_LIB_DIR=/path/to/openssl/arm64-v8a
./scripts/build-android.sh --check
./scripts/build-android.sh --debug
```

The script derives its persistent build directory under `/workspace/betterflash-build` from the complete worktree path and a SHA-256 path prefix, sets native API 28, and requests Qt's `apk` target with parallel compilation. `BETTERFLASH_ANDROID_BUILD_DIR` overrides the build location; an existing CMake cache must point at this worktree. `--release` prepares a release build; device deployment and signing are separate actions. With Fedora's system host Qt, `QT_HOST_ROOT=/usr` is accepted when its version matches the Android kit.

An Android APK has not been built or tested yet because the Android kit, SDK, NDK, full JDK, and arm64 OpenSSL assets are missing from this machine. After installing them, the next checks are package startup, phone and tablet layouts, image import, voice permission denial/retry, Bluetooth audio, provider cancellation during lifecycle changes, and two-device synchronization.

Image import currently accepts local file URLs. Android document-provider
`content:` URLs still need a read adapter before phone attachment import is
complete.
