# Third-party components

BetterFlash uses the system Qt installation for its native interface, SQLite
driver, networking, audio, and speech synthesis. Qt license notices are supplied
by that installation. Android packaging must retain the notices provided by its
Qt kit and OpenSSL libraries.

The build fetches pinned source archives and verifies their SHA-256 checksums:

| Component | Version | License and source |
| --- | --- | --- |
| MicroTeX | `0e3707f6dafebb121d98b53c64364d16fefe481d` | [MIT](packaging/licenses/MicroTeX-MIT.txt), [source](https://github.com/NanoMichael/MicroTeX/tree/0e3707f6dafebb121d98b53c64364d16fefe481d) |
| tinyxml2 | `11.0.0` | [zlib](packaging/licenses/tinyxml2-zlib.txt), [source](https://github.com/leethomason/tinyxml2/tree/11.0.0) |
| llama.cpp | `8bb3cf0d216a5ae48360179f2931daf284be4d01` | [MIT](packaging/licenses/llama.cpp-MIT.txt), [source](https://github.com/ggml-org/llama.cpp/tree/8bb3cf0d216a5ae48360179f2931daf284be4d01) |
| SPIRV-Headers | `vulkan-sdk-1.4.341.0` | [notices](packaging/licenses/SPIRV-Headers-notices.txt), [source](https://github.com/KhronosGroup/SPIRV-Headers/tree/vulkan-sdk-1.4.341.0) |
| libarchive headers | `3.8.1` | [notices](packaging/licenses/libarchive-notices.txt), [source](https://github.com/libarchive/libarchive/tree/v3.8.1); links the system runtime |

llama.cpp's common library includes nlohmann JSON and cpp-httplib. Their MIT
notices are retained in `packaging/licenses/`. Its pinned source needs an
explicit `<algorithm>` include in `clef.cpp`; the CMake adapter adds this include
without changing inference behavior.

Local language weights download separately from the app. Gemma 4 E2B uses the
[Apache 2.0 terms published by Google](https://huggingface.co/google/gemma-4-E2B-it),
with a pinned [Unsloth GGUF conversion](https://huggingface.co/unsloth/gemma-4-E2B-it-GGUF).
LFM 2.5 350M uses the [Liquid model license](https://huggingface.co/LiquidAI/LFM2.5-350M-GGUF/blob/657e078c94084481950a2d555a941481f715536b/LICENSE).
The revisions, byte counts and hashes for both are recorded in
`src/models/LocalLanguageModel.cpp`. Model weights are not committed here.

IBM Plex Mono regular, bold, italic, and bold italic are embedded without
modification for the interface and Markdown renderer. These font files are
distributed under the [SIL Open Font License](packaging/licenses/IBM-Plex-Mono-OFL.txt)
and were obtained from the [Google Fonts distribution](https://github.com/google/fonts/tree/0b58fb370093f9a9f4ff785d94405710b79de67c/ofl/ibmplexmono).

MicroTeX's Qt text painter is adapted at build time to use glyph outlines. Its
formula fonts and resource files are bundled without changing the fonts. Their
upstream notices include the SIL Open Font License, Knuth's font notice, the
dsrom notice, and the Greek and Cyrillic font licenses. Copies are in
`packaging/licenses/` and in the application resources.

## Managed local speech

Local speech is the default configuration. The application downloads verified
runtime/model packages from upstream on first use; they are not committed as
binaries in this repository. `src/voice/VoiceController.cpp` pins the archives,
sizes, and SHA-256 checksums, and `cmake/LocalSpeech.cmake` independently pins
the official Sherpa C API header used to describe the downloaded runtime ABI.

| Component | Pinned source | License |
| --- | --- | --- |
| Vosk native runtime | [Vosk API 0.3.45](https://github.com/alphacep/vosk-api/tree/v0.3.45) and its official Linux x64 wheel | [Apache-2.0](https://github.com/alphacep/vosk-api/blob/v0.3.45/COPYING) |
| Vosk small English model | [vosk-model-small-en-us-0.15](https://alphacephei.com/vosk/models) | Apache-2.0 as listed in the model catalog |
| Sherpa-ONNX C API/runtime | [Sherpa-ONNX 1.10.46](https://github.com/k2-fsa/sherpa-onnx/tree/v1.10.46) | [Apache-2.0](https://github.com/k2-fsa/sherpa-onnx/blob/v1.10.46/LICENSE) for Sherpa's own code |
| Embedded espeak-ng phonemizer | [csukuangfj/espeak-ng f6fed6c58b5e0998b8e68c6610125e2d07d595a7](https://github.com/csukuangfj/espeak-ng/tree/f6fed6c58b5e0998b8e68c6610125e2d07d595a7) | [GPL-3.0-or-later](https://github.com/csukuangfj/espeak-ng/blob/f6fed6c58b5e0998b8e68c6610125e2d07d595a7/src/libespeak-ng/speech.c), [license text](https://github.com/csukuangfj/espeak-ng/blob/f6fed6c58b5e0998b8e68c6610125e2d07d595a7/COPYING) |
| Sherpa's companion ONNX Runtime | [ONNX Runtime 1.17.1](https://github.com/microsoft/onnxruntime/tree/v1.17.1) | [MIT](https://github.com/microsoft/onnxruntime/blob/v1.17.1/LICENSE) |
| Piper LJSpeech medium voice | [vits-piper-en_US-ljspeech-medium package](https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/vits-piper-en_US-ljspeech-medium.tar.bz2) | Included `MODEL_CARD` identifies the [public-domain LJSpeech source dataset](https://keithito.com/LJ-Speech-Dataset/) |
| Piper Amy low voice | [vits-piper-en_US-amy-low package](https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/vits-piper-en_US-amy-low.tar.bz2) | Included `MODEL_CARD` points to [Mycroft's voice repository](https://github.com/MycroftAI/mimic3-voices) and its [CC-BY-SA-4.0 license](https://github.com/MycroftAI/mimic3-voices/blob/master/LICENSE); it also records fine-tuning from the Ryan low voice |

The espeak-ng inclusion is part of the actual linked runtime, not merely a
development dependency. The pinned Sherpa release's
[CMake source](https://github.com/k2-fsa/sherpa-onnx/blob/v1.10.46/cmake/espeak-ng-for-piper.cmake)
builds that espeak-ng revision as a static library, and the downloaded
`libsherpa-onnx-c-api.so` exports espeak functions. Consequently the speech
runtime is not uniformly Apache-2.0 or MIT. Distribution must address the
GPL-3.0-or-later obligations for the linked combination, including appropriate
licensing, notices, and corresponding source. Dynamic loading and separate
model downloads do not establish a licensing exception. The pinned source
repositories and Sherpa's dependency definitions identify the corresponding
source starting points; this development setup is not a complete distribution
source offer or an audit of every transitive native dependency.

Verified runtime license texts are downloaded alongside the assets under
`models/voice/stt/licenses/` and `models/voice/tts/licenses/` in Qt's per-user
application data directory. The complete voice archive is retained, including
its `MODEL_CARD` and phonemizer data. These cached notices explain the installed
assets, while a release package still needs its own notices and source
arrangements. A voice model's source dataset terms are recorded separately
from the inference engine's software license. In particular, the LJSpeech
dataset's public-domain status is not a blanket license for unrelated voices.

Remote providers retain their own service terms; no provider credentials are
included in this repository. Groq model validation uses the pinned official
Vosk example recording and only when the user selects a Groq test.

Local semantic search can use quantized ONNX exports of the BGE sentence
transformers. Desktop defaults to `Xenova/bge-base-en-v1.5` (768 dimensions);
compact targets use `Xenova/bge-small-en-v1.5` (384 dimensions). The model
assets download on first semantic search or through Search settings, are pinned to revisions in
`src/search/LocalEmbedder.cpp`, and are verified by SHA-256 before use. Their
model cards provide the MIT terms in
`packaging/licenses/BGE-MIT.txt`.

The native inference runtime is ONNX Runtime 1.20.1 for Linux x86_64 and the
1.20.0 Android AAR, fetched from official release/Maven archives with pinned
SHA-256 checksums. It is distributed under the
MIT License; see `packaging/licenses/ONNX-Runtime-MIT.txt`.

Markdown code highlighting links KDE Syntax Highlighting 6.18.0, fetched from
[the official KDE archive](https://download.kde.org/stable/frameworks/6.18/syntax-highlighting-6.18.0.tar.xz)
with a pinned SHA-256. Its library code is MIT licensed; its bundled language
definitions include MIT, LGPL, GPL and CC0 material with individual notices in
the definition resources. Aggregate license texts are supplied as
`packaging/licenses/KSyntaxHighlighting-*.txt`. Extra CMake Modules 6.18.0 is a
build-time dependency fetched from KDE with a pinned SHA-256; it is not linked
into the application. Upstream source archives remain available at the URLs
recorded in `cmake/SyntaxHighlighting.cmake`.

Additional code palettes adapt the upstream Rose Pine, Everforest, Jellybeans
and Iceberg themes. Their MIT notices are in `packaging/licenses/`; the palette
conversions and source links are in `src/render/themes/`. Gruvbox and the other
KDE themes retain their upstream resource notices. BetterFlash adjusts code
foreground colors where necessary for contrast. `clangd` is an optional external
program installed separately; it is not redistributed by this project.
