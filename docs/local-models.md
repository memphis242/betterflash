# Local model support

BetterFlash is local-first for the language, speech, and embedding features.
The application downloads pinned assets only when a feature needs them or when
the user chooses **Prepare model** in Settings. Downloads show progress, verify
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
model is reported ready. The check never sends collection content or requires
an API key.

## Speech

Fedora x86-64 uses the managed Vosk small English recognition model and a
native runtime. Local speech synthesis uses a managed Piper/Sherpa-ONNX voice;
the compact Amy low profile and desktop LJSpeech medium profile are available.
Speech assets are prepared on demand and loaded only while recognition or
synthesis is active. See [Voice review](voice.md) for package sizes, validation,
licensing notes, and the current Android boundary.

## Embeddings

Semantic search uses BGE base English v1.5 on desktop and BGE small English
v1.5 for the compact Android profile. ONNX Runtime performs local inference and
the embedding cache is kept with the collection. The Search settings page
offers model preparation and a known-input validation. Remote embeddings are
optional and require an explicitly configured BYOK endpoint.

## Resource policy

Only one local model operation is started for a feature at a time. The
application checks available resources before loading a larger language model,
limits worker threads, and surfaces a stable error when memory or model context
is insufficient. Selecting a smaller profile or CPU execution is the recovery
path. Local models are optional on Android until compatible Android-native
speech and language assets are packaged and tested on a device.
