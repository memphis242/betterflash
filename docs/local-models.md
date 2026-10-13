# Local model support

BetterFlash is local-first for the language, speech, and embedding features.
The application downloads pinned assets only when a feature needs them or when
the user chooses **Download local model** in Settings. Downloads show progress, verify
their expected size and SHA-256, and can be canceled. Verified assets remain in
the per-user BetterFlash model directory for later offline use.

## Language models

The desktop language-model default is **Automatic**. On Linux it selects Gemma
4 E2B Q4_K_M when the machine reports at least 12 GiB of system memory. That
asset is approximately 3.1 GB. Machines below that threshold use LFM 2.5 350M
Q4_K_M, approximately 229 MB. Android selects LFM 2.5 350M for its smaller
memory budget. Both profiles use native llama.cpp code and support CPU
execution; automatic acceleration uses a suitable available llama.cpp GPU
backend when it has enough free memory, then falls back to CPU. CPU can be
selected explicitly.

Local inference has bounded context, response, and wall-clock limits. Model
objects and contexts are released after an operation, so an idle application
does not retain the full model allocation. A built-in known-input check sends a
small fixed request and verifies the expected structured response before a
check is reported passed. Download readiness means the verified files are
available; it does not mean inference validation has passed. Local checks never
send collection content or require an API key.

## Downloading and testing

The blue download control fetches and verifies the selected model files, with
archive extraction where needed, and stores them for later offline use. This is
what the earlier **Prepare** label meant. Downloading does not load the model
into memory. A yellow **Test model** control runs validation separately and
never downloads missing model weights or inference runtimes. Local tests become
available after their model files are ready.

Each test has a bounded result box showing its input, expected behavior, and
actual output. Embedding tests include both text pairs and their measured cosine
similarities. Failed semantic ordering retains those scores for inspection.
Speech recognition uses the official Vosk sample recording, downloaded as a
small validation fixture if needed. Speech synthesis reports generated audio
and playback completion; it does not transcribe its own output or prove the
voice's subjective quality. Listen to the sample to evaluate its clarity.

## Speech

Fedora x86-64 defaults to managed Whisper Large v3 Turbo Q8_0 (874.2 MB),
executed by a bundled whisper.cpp helper. The Vosk English 0.22 lgraph
streaming profile remains available. The compact Vosk small English profile remains available for
lower-memory devices and is the intended Android profile. Local speech synthesis uses a managed Piper/Sherpa-ONNX voice;
the compact Amy low profile and desktop LJSpeech medium profile are available.
Speech assets are downloaded on demand. See [Voice review](voice.md) for package sizes, validation,
licensing notes, and the current Android boundary.

## Embeddings

Semantic search uses BGE base English v1.5 on desktop and BGE small English
v1.5 for the compact Android profile. ONNX Runtime performs local inference and
the embedding cache is kept with the collection. The Search settings page
offers model downloads and a known-input validation. Remote embeddings are
optional and require an explicitly configured BYOK endpoint.

## Execution runtimes

Inference uses native code driven by background workers. Whisper runs in
a bundled child process to isolate its backend from llama.cpp; the other
models use libraries inside the BetterFlash process. The GUI thread handles presentation; it does not execute model inference.
There is no Python interpreter or separate local inference server.

| Feature | Model files and execution | Current device support | Allocation lifetime |
| --- | --- | --- | --- |
| Language | Gemma/LFM quantized GGUF through llama.cpp | CPU, or Vulkan GPU with sufficient free memory | One request at a time; model and context released after completion |
| Recognition | Whisper Turbo Q8_0 through whisper.cpp; optional Vosk balanced/compact through its C API | Whisper: CPU or optional Vulkan with CPU fallback. Vosk: CPU | Whisper releases its process and model after each utterance. Vosk stays loaded while recognition is enabled; validation releases it afterward |
| Speech output | Piper ONNX voices through Sherpa-ONNX, with eSpeak phonemization | CPU, up to four inference threads | Loaded lazily; voice model released after 30 seconds idle |
| Embeddings | Quantized BGE ONNX through ONNX Runtime | CPU, four desktop threads or two compact threads | Search worker retains its selected model for reuse; standalone validation releases its model after the check |

llama.cpp CPU execution uses at most eight threads, with 8,192-token Gemma
contexts and 4,096-token LFM contexts. Vulkan support is built using `glslc`;
installing that compiler does not itself guarantee a suitable GPU is available.
The actual LLM execution device appears in Settings after inference runs.
The embedding runtime is ONNX Runtime 1.20.1 on Linux. The managed Sherpa speech
runtime uses its own compatible ONNX Runtime 1.17.1 library. Both presently use
the CPU execution provider.

Cloud choices send requests to the configured provider over HTTPS. Groq speech
recognition uploads audio; remote language and embedding requests send their
input text. System speech output delegates to Qt's installed speech engine.
Whisper Turbo Q8_0 with beam decoding took about 3.6 seconds including loading
on the development CPU for a 7.8-second acronym recording. This is one sample,
not a cross-device latency guarantee. Optional local language-model transcript
correction adds a separate inference operation; raw and corrected custom test
results are displayed independently.

## Resource policy

Only one local model operation is started for a feature at a time. The
application checks available resources before loading a larger language model,
limits worker threads, and surfaces a stable error when memory or model context
is insufficient. Selecting a smaller profile or CPU execution is the recovery
path. Local models are optional on Android until compatible Android-native
speech and language assets are packaged and tested on a device.
