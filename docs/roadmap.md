# Roadmap

## Android completion

- Build with a real Qt Android kit, SDK, NDK, JDK, and arm64 OpenSSL package.
- Verify startup, layout, image import, microphone permission handling, speech
  lifecycle behavior, memory use, model cancellation, and synchronization on a
  physical device.
- Package compatible Android-native speech and language assets before claiming
  offline voice or local language inference is complete.

## Windows support - low priority

Windows work follows the Android build. The intended first toolchain is Qt's
MinGW-w64 kit with CMake; Visual Studio is not required for that route. The
port still needs a Windows Qt build, Windows packaging, ONNX Runtime Windows
assets, native credential storage, portable media backup code, and replacements
for Linux-only integrations such as Secret Service, `/proc` memory detection,
and the current local speech packages. The Windows build should be treated as a
separate compatibility milestone after Android device verification.

## Later local-model work

- Add more hardware profiles and memory telemetry after testing on target
  devices.
- Evaluate additional speech runtimes and Android-compatible packages.
- Revisit GPU backends and model quantization after measuring startup, memory,
  and review latency.

## Future hands-free voice review

- Add a fully hands-free mode with a wake phrase and clear controls for false
  activations, background noise, microphone device changes, and lifecycle state.
- Define how hands-free voice review behaves across application navigation and
  other screens before enabling it outside review.

## In-app usage and configuration help

- Explore an optional language-model help feature using the selected local or
  remote model. Help should explain the application's current controls and
  settings in plain language, including starting a particular deck's review,
  resetting a deck's review state, and configuring model connections.
- Ground answers in versioned application documentation and the current screen
  so instructions match the installed application. Keep credentials out of
  help prompts. Guidance and any future user-requested actions need separate
  interaction design; this feature is a to-do, not an implemented help panel.
