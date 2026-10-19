# Android implementation plan and session handoff

Status: planning only. Research recorded **2026-10-19**.

## Start here

- The accepted desktop baseline is **v0.1.0-beta**, targeting Fedora Linux.
- The first Android device is the user's **Samsung Galaxy S26 Ultra**, running
  **One UI 9.0 / Android 17**. Confirm its actual build, API, ABI and page size
  through ADB once connected; do not infer RAM or available memory from its name.
- The user requested research and a plan, **not Android implementation yet**.
  Follow their next instruction before starting the phases below.
- Automated tests remain paused until explicitly requested. Reading files,
  checking installed tools and the environment-only build preflight were the
  extent of local investigation. No Android build or device validation occurred.
- The user authorized consolidating completed branches into `main` and removing
  redundant branches/worktrees. Start future work from the primary checkout at
  `/home/aalmo/projects/betterflash`, inspect Git state, and read `AGENTS.md`.
  Do not rely on former feature-worktree paths or old launch commands.
- Use a fresh isolated worktree for Android/backend work with its own build
  directory. Do not point a new worktree at the primary checkout's CMake cache.
- Recheck online toolchain requirements at implementation time. Versions below
  are researched choices, not proof that the current build script accepts them.

## Product and architecture decisions

Retain C++23, Qt Quick and QML. Share the scheduling, SQLite data model,
Markdown/LaTeX renderer, bundled syntax highlighting, search, sync, checkpoints,
undo, study guidance and LLM services. Add a small Android-specific integration
layer for credentials, file access, permissions, audio and lifecycle handling.
No Electron, embedded browser, Python model sidecar or wholesale UI rewrite.

Adapt QML to phone use rather than shrinking the desktop layout. Retain the
warm dark theme, separate light palette, walnut identity and consistent rating
colors. Apply the `gui-principles` skill and project interaction preferences.

The first target is one arm64 phone. Emulator coverage, other Android hardware,
Windows and iOS follow useful physical-device milestones. Windows remains after
Android, with iOS after Windows. VPS deployment details remain deferred.

## Environment audit

Observed on the Fedora development PC on 2026-10-19:

| Component | Observed state |
| --- | --- |
| Desktop Qt | Fedora Qt 6.11.2 development packages installed |
| CMake / Ninja | Installed |
| Java | OpenJDK 25 runtime present; `javac` not on PATH |
| Full JDK 21 | `java-21-openjdk-devel` not installed |
| ADB | Not on PATH; Fedora `android-tools` not installed |
| Android Qt kit | Not configured/found by preflight |
| SDK / NDK | Not configured/found by preflight |
| Android OpenSSL | Not configured/found by preflight |
| Native syntax indexer | Available from the desktop build used for inspection |

`scripts/build-android.sh --check` reported six missing prerequisite groups:
Android Qt kit, matching host Qt, full JDK, SDK, NDK and arm64 OpenSSL. A missing
configured path is not proof that a package exists nowhere on the filesystem.
Do not install duplicate toolchains without checking user-provided locations.

### Recommended toolchain

Install **Qt 6.12.0** side by side with the existing desktop setup. It was
released on 2026-09-30. Select matching Linux `gcc_64` host tools and Android
`arm64-v8a`, plus Core, Concurrent, Gui, Quick, Quick Controls, Sql, Network,
Multimedia and TextToSpeech. Qt Creator is useful for its Android setup wizard;
Android Studio is optional for this CMake-based project.

Both Qt 6.11 and Qt 6.12 published support tables currently stop at Android 16,
API 36. Running on Android 17 is therefore an early compatibility milestone,
not an established support claim. A newer Qt release alone does not resolve it.

| Component | Planned starting configuration |
| --- | --- |
| Qt | 6.12.0 host + Android arm64 kits, exactly matching versions |
| JDK | Full JDK 21, selected explicitly for Android builds |
| NDK | r27c, `27.2.12479018`, matching Qt's published configuration |
| SDK platforms | API 36 for initial Qt configuration; API 37 for Android 17 |
| Build Tools | 36.0.0 initially; revisit with the API 37 transition |
| Platform Tools | Current ADB and related tools |
| HTTPS | Android arm64 OpenSSL `libcrypto_3.so` and `libssl_3.so` |

Keep compile SDK, target SDK and minimum supported runtime distinct. The draft
uses minimum API 28 and target API 36. An API 36-targeted application can be
attempted on an Android 17 device; that does not establish compatibility with
all API 37-targeted behavior. Move to target API 37 after reviewing those changes.
There is no current requirement to drop older runtime compatibility merely
because the first device uses Android 17.

### User setup steps

1. Install Fedora development/device tools:

   ```bash
   sudo dnf install java-21-openjdk-devel android-tools
   ```

2. Use the Qt installer to select the matching Qt host/Android kits and Qt
   Creator. Qt Creator's Android setup wizard can manage the SDK, NDK and
   Android OpenSSL. Leave the existing Fedora desktop Qt installation intact.
3. Install SDK platforms/tools. With Google's command-line tools configured:

   ```bash
   sdkmanager \
     "platform-tools" \
     "platforms;android-36" \
     "platforms;android-37" \
     "build-tools;36.0.0" \
     "ndk;27.2.12479018"
   ```

4. Review and accept the SDK licenses when prompted. Do not automatically
   accept terms on the user's behalf.
5. Provide the installed paths. The build environment needs:

   - `JAVA_HOME`: full JDK 21, containing both `java` and `javac`.
   - `QT_HOST_ROOT`: matching Qt Linux host installation.
   - `QT_ANDROID_ROOT`: Qt Android arm64 installation.
   - `ANDROID_HOME` and the script's `ANDROID_SDK_ROOT`: the same SDK directory.
   - `ANDROID_NDK_ROOT`: SDK's `ndk/27.2.12479018` directory.
   - `ANDROID_OPENSSL_LIB_DIR`: directory containing the Android arm64 libraries.

Use `/workspace` for persistent SDKs, Qt kits, build trees and model caches.
Reserve roughly 30 GB initially as a planning allowance, not a measured package
size. An emulator and its system images are optional initially.

The agent can handle project configuration and OpenSSL integration after the
paths and necessary user-controlled setup are available. Do not ask the user to
manually resolve source-level porting work.

### Phone setup

1. Settings > About phone > Software information > tap Build number seven times.
2. Enable USB debugging in Developer options.
3. Connect a data-capable USB cable and accept the computer authorization prompt.
4. If USB commands/debugging are blocked, inspect Samsung Auto Blocker settings.
   Adjust only what is needed for development; do not assume every device has
   the same policy or requires broad security changes.
5. Inspect the actual device:

   ```bash
   adb devices -l
   adb shell getprop ro.build.version.release
   adb shell getprop ro.build.version.sdk
   adb shell getprop ro.product.cpu.abilist
   adb shell getconf PAGE_SIZE
   ```

Use a single selected ADB installation consistently if both Fedora and the SDK
provide it. Address Fedora USB device permissions if needed; do not routinely
run the application or ADB server as root. Wireless debugging can be added later.

## Existing implementation and concrete gaps

Read `docs/android.md` for the draft and the following source boundaries:

| Area | Existing work | Gap / first investigation |
| --- | --- | --- |
| Packaging | `android/AndroidManifest.xml`, `scripts/build-android.sh`, Android CMake target | No APK built; script hardcodes Qt 6.11, API 36 and NDK r27c |
| Shared core | `src/core/`, SQLite worker thread, transactions, outbox | Cross-compile C++23/libc++, especially `std::expected`; verify POSIX media code |
| Rendering | `src/render/`, native TeX and bundled syntax data | Host `katehighlightingindexer` required; external language servers disabled on Android |
| Credentials | `src/integrations/SecretStore.cpp` | Desktop Secret Service relies on `secret-tool`; Android persistence is session-only |
| Images | `src/integrations/MediaStore.cpp` | Rejects document-provider `content:` URIs |
| Models | `src/models/`, llama.cpp; Android ONNX AAR path | Native build, memory and model quality unverified; pinned Android ONNX version needs compatibility review |
| Speech | `src/voice/`, runtime microphone permission | Managed speech assets/Whisper executable are desktop-specific; native Android inference needed |
| Lifecycle | `src/main.cpp` disables voice outside foreground | Process recreation, draft/session recovery, background voice incomplete |
| Sync | Shared controller, protocol and media transfer | Actual two-device behavior and Android TLS still unverified |

Do not weaken secure media validation to make file pickers work. Read authorized
`content:` streams and import into the existing content-addressed store.

## Phased implementation

### Phase 1: Installable native shell

Deliver an arm64 debug APK that launches on the S26 Ultra.

- Update build/preflight configuration for the selected Qt kit, without
  reconfiguring unrelated desktop build trees.
- Verify C++23/libc++ and native dependency compatibility. Do not assume a
  `std::expected` failure in advance; inventory actual compiler errors.
- Compile and package Qt plugins, SQLite, fonts, Markdown, LaTeX and syntax data.
- Verify HTTPS loads the packaged OpenSSL libraries and handles certificate errors.
- Inspect every native shared library for 16 KB page-size compatibility, including
  ONNX, OpenSSL, Qt and model libraries. NDK r27 requires explicit alignment care.
- Establish repeatable build/install/launch/log capture commands.
- Pin dependency versions and checksums; do not silently fetch unpinned upgrades.

Exit condition: app launches and opens an isolated sample collection on the
actual phone. Desktop compilation is not evidence of this milestone.

### Phase 2: Phone review and library layouts

Deliver useful offline deck browsing, editing and review.

- Compact navigation drawer on narrow displays; preserve desktop layouts at
  wider sizes and support physical-keyboard bindings.
- One primary pane at a time for decks, cards and details. Explicit action menus
  replace reliance on right-click; touch alternatives are required.
- Comfortable rating/microphone targets and an optionally collapsed queue.
- Full-screen detail/editor treatments where desktop dialogs become cramped.
- Handle Android Back, display cutouts/insets, keyboard visibility, rotation,
  larger fonts and touch scrolling. Keep drafts and context during navigation.

Exit condition: import sample data, complete an offline review, edit/save a card,
and navigate without clipped or unreachable controls.

### Phase 3: Android storage, credentials and lifecycle

- Encrypt persisted API-key strings with a key protected by Android Keystore.
  Never place API keys in collection SQLite, sync, logs or backups.
- Add authorized document-provider reads for images, recordings and backup
  import/export as applicable. Copy imports into app-private managed storage.
- Handle denial/revocation of microphone and document access with actionable UI.
- Preserve saved changes, drafts and necessary review context across activity
  recreation and process death. Do not depend on destructor execution to save.
- Distinguish session-only undo from durable recovery. Persisting the five-action
  undo history across process death is a separate extension, not current behavior.

Exit condition: attach an image, record/play narration, restart the app, and
recover intended saved state and credentials without leaking secrets.

### Phase 4: Desktop/phone synchronization

- Use the shared event protocol/outbox, never direct concurrent SQLite-file sync.
- Verify deck/card edits and moves, grades, independent reverse/cloze schedules,
  managed images and narration in both directions.
- Handle intermittent connectivity, interrupted media transfer and conflicting
  edits without silently overwriting newer content.
- Surface pending operations and sync failures. Preserve local offline use.
- Keep checkpoint-history synchronization explicitly deferred unless separately
  requested; current checkpoint history is local, restored content queues sync.
- Coordinate server/client protocol versions. Deployment specifics remain deferred.

Exit condition: work offline on the phone, reconnect, and observe consistent
content/history on desktop without duplicate review mutations.

### Phase 5: Model features and resource policy

| Feature | Initial direction |
| --- | --- |
| Embeddings | Existing compact BGE profile using native ONNX Runtime |
| LLM | Existing compact LFM 2.5 350M llama.cpp profile; measure usefulness |
| STT | Android-native Whisper integration; compare candidates on actual recordings |
| TTS | Evaluate installed Android TTS voices, including offline availability |
| Custom narration | Preserve precedence over generated speech |
| Remote providers | Retain explicit BYOK options for all supported categories |
| LLM actions | Reuse summaries, atomicization, study help, correction and Generate like this |

Local remains the intended sensible default; remote options also provide a useful
reference while local quality is evaluated. Do not automatically send content to
a cloud provider when a local model fails. Run inference only on explicit user
request or an enabled interaction, and retain model tests/download controls.

- Load models on demand and bound concurrency, context, memory and thread counts.
  Do not keep all four model types resident merely because the phone is high-end.
- Establish CPU baselines. ONNX recommends CPU first for quantized models and
  XNNPACK for suitable floating-point models; measure before adding accelerators.
  GPU/NPU benefits depend on runtime, operators and device drivers.
- Port Whisper to an Android library/JNI boundary instead of launching the
  desktop executable. Use `tests/sbng.wav` if present and the user's custom
  recordings to evaluate abbreviation quality. Do not choose solely by model size.
- Measure end-to-end latency, peak memory, thermal behavior and battery use with
  optimized native builds. Keep resource/quality fallback choices understandable.
- Android 17 documents app memory limits; respond to memory pressure rather than
  disabling system protections or assuming all device RAM is available to us.

Exit condition: on-device search and at least one useful local model path,
configurable remote models, explicit failures/cancellation, no UI blocking.

### Phase 6: Foreground Voice Control, then locked-screen sessions

First deliver foreground interaction with touch-triggered microphone control,
clear listening/processing/speaking/error states and the existing utterance
submission policy. Preserve recognition vocabulary, optional LLM correction,
code-skipping TTS and per-side custom narration.

Handle permission denial/retry, Bluetooth routing, audio focus, interruptions,
headset changes and TTS feedback into the microphone. Never interpret model
prose as arbitrary application actions.

Treat always-on/locked-screen review as a separate later milestone:

- A deliberately started Android microphone/playback foreground service.
- Appropriate permissions/service types, visible notification and stop controls.
- A defined lifetime independent of the activity and recovery after interruption.
- Audio focus and Samsung lifecycle/battery behavior verification.
- Android 17 background-audio rules, including stronger target-37 restrictions.

Do not claim screen-off or background voice works because foreground recording
works. Current Android code intentionally disables voice on backgrounding.

### Phase 7: Personal beta distribution

Once the app is useful on the phone, establish repeatable signed APK updates,
stable application identity and safe migrations. Keep signing material private
and backed up, and avoid uninstalling the user's application merely to resolve
an update/signing mismatch. Preserve explicit export/backup paths.

Google Play distribution, App Bundles and wider device support are optional
later scope. Initial personal testing can use direct APK installation.

## First useful milestone

Install on the S26 Ultra, import an example collection, complete a review
offline, edit a card, attach an image and reopen without losing saved work.
Then expand sync, models and Voice Control in the phases above.

These are future manual acceptance checks, not authorization to run or add an
automated test suite. The user will request the broader test/bug-hunt phase.

## Official research sources

Consulted 2026-10-19. Revalidate before installing or changing versions.

- [Qt 6.12 release](https://www.qt.io/blog/qt-6.12-released)
- [Qt 6.12 Android supported configurations](https://doc.qt.io/qt-6.12/android.html)
- [Qt 6.11 Android prerequisites](https://doc.qt.io/qt-6.11/android-configure-dev-environment.html)
- [Qt Android OpenSSL](https://doc.qt.io/qt-6.11/android-openssl-support.html)
- [Android 17 SDK setup](https://developer.android.com/about/versions/17/setup-sdk)
- [Android 17 changes affecting all apps](https://developer.android.com/about/versions/17/behavior-changes-all)
- [Android 17 target-specific changes](https://developer.android.com/about/versions/17/behavior-changes-17)
- [16 KB page-size compatibility](https://developer.android.com/guide/practices/page-sizes)
- [Android hardware-device setup](https://developer.android.com/studio/run/device)
- [Samsung Developer options](https://developer.samsung.com/health/data/guide/phone-developer-options.html)
- [Samsung Auto Blocker](https://www.samsung.com/us/support/answer/ANS10003636/)
- [Fedora full JDK 21](https://packages.fedoraproject.org/pkgs/java-21-openjdk/java-21-openjdk-devel/)
- [Fedora Android tools](https://packages.fedoraproject.org/pkgs/android-tools/android-tools/)
- [Android Keystore](https://developer.android.com/privacy-and-security/keystore)
- [Android document access](https://developer.android.com/guide/topics/providers/document-provider)
- [Foreground-service types](https://developer.android.com/develop/background-work/services/fgs/service-types)
- [Android 17 background audio](https://developer.android.com/about/versions/17/changes/bg-audio)
- [ONNX Runtime mobile guidance](https://onnxruntime.ai/docs/tutorials/mobile/)
- [Whisper.cpp Android example](https://github.com/ggml-org/whisper.cpp/blob/master/examples/whisper.android/README.md)
