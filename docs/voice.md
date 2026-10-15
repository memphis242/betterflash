# Voice review

Speech recognition and reading are local by default. Settings > Voice keeps
provider fields on the page and exposes a separate **Test model** action for
recognition and reading. A single model box contains the location selector,
configuration, status, downloads, and basic model test, with space separating
it from the feature's other settings. **Location of model:** uses a compact
Local/Remote selector with a yellow local selection and blue remote selection,
matching Language model and Search settings. Groq recognition and remote
speech output are explicit alternatives. The operating system's Qt speech
engine remains a local speech-output choice. Voice starts disabled, so opening the app
does not start a microphone or load inference models.

## Voice review listening modes

Voice review uses **Keyboard trigger** by default. Enabling voice review in
Settings prepares recognition without opening the microphone. Press the
configured microphone shortcut (M by default) to enable voice if needed and arm
one utterance. Press the shortcut again to cancel. The utterance ends after
silence and the microphone stops. The shortcut can be changed under Settings >
Key bindings; its current binding is shown in the Voice review settings help.

**Always on** is an explicit opt-in mode. While voice review is enabled, it
listens between utterances. The review microphone control starts or pauses the
continuous listener, and disabling voice review in Settings stops it.

## Managed local speech

On Fedora x86-64, enabling voice or using a prepared local model uses Whisper Large v3 Turbo Q8_0 through the bundled native
`betterflash-whisper` helper. Vosk's balanced and compact streaming profiles
remain selectable alternatives. The
**Download local model** action obtains those files; testing requires them to be
prepared and does not download them. Reading uses the same separate download
step for a Piper voice with a pinned Sherpa-ONNX runtime. No
Python package, standalone speech daemon, or command-line model server is
needed. Downloads show progress and can be canceled; SHA-256 and exact byte
sizes are verified before safe archive extraction. The application retains
verified files for later offline use. The first download needs Internet access.

| Purpose | Default | Download | Inference |
| --- | --- | --- | --- |
| Desktop recognition | Whisper Large v3 Turbo Q8_0 | 874.2 MB model; bundled whisper.cpp helper | CPU, up to eight threads; optional Vulkan GPU with CPU fallback |
| Streaming recognition | Vosk English 0.22 lgraph | 130.6 MB model + 7.2 MB runtime | CPU, native streaming recognition |
| Compact recognition | Vosk small English 0.15 (explicit compact profile) | 41.2 MB model + shared runtime | CPU, native streaming recognition |
| Desktop reading | Piper LJSpeech medium | 67.2 MB voice + 20.5 MB runtime | CPU, at most four threads, 22,050 Hz |
| Compact reading profile | Piper Amy low | 67.1 MB voice + shared runtime | CPU, at most four threads, 16,000 Hz |

The compact voice primarily lowers synthesis work; its floating-point model has
approximately the same size as the desktop voice. Both voices have roughly
63 MB of weights. Models load only when used. Whisper loads its model in a separate process for each utterance and releases
it when transcription completes or is canceled. Its isolated process keeps
whisper.cpp and llama.cpp backend symbols separate. Vosk releases its model
when voice is disabled. The reading worker releases weights after 30 seconds
idle and when voice is disabled, while verified downloads remain on disk.
Piper's small model runs on CPU in this first implementation. GPU use is not
claimed for this runtime; acceleration and quantized speech profiles remain
future hardware work.

The cache lives below Qt's per-user application data directory at
`models/voice/`. Recognition's editable advanced model path can also refer to a
previous local installation. **Download model** restores the managed path.
`BETTERFLASH_VOSK_LIBRARY` and `BETTERFLASH_WHISPER_EXECUTABLE` remain
optional development overrides. The latter must name an executable file. Model
archives, runtime archives, and the validation recording all have hard-coded
checksums in `VoiceController`; the C API header is pinned independently in
`cmake/LocalSpeech.cmake`.

The pinned Sherpa-ONNX runtime includes its matching ONNX Runtime 1.17.1 shared
library, with an independent SONAME/version from the application's search
runtime 1.20.1. Keeping its companion library beside the C API library preserves
the release's own runtime search path and binary interface.
[Sherpa-ONNX release](https://github.com/k2-fsa/sherpa-onnx/releases/tag/v1.10.46),
[Piper model catalog](https://k2-fsa.github.io/sherpa/onnx/tts/pretrained_models/vits.html),
[Vosk models](https://alphacephei.com/vosk/models),
[Vosk native runtime](https://pypi.org/project/vosk/0.3.45/)

The archives retain each voice's `MODEL_CARD`; separately pinned license texts
download into `stt/licenses/` and `tts/licenses/`. LJSpeech's model card records
its public-domain source dataset. Amy's card points to Mycroft's voice terms
and records its training lineage. The Sherpa library incorporates GPL-3.0-or-later
espeak-ng code as a static component, confirmed by the pinned build definitions
and exported espeak symbols. The linked runtime is not uniformly permissive.
Redistribution must address GPL licensing, notices, and corresponding source
for the linked combination. Cache notices and source links do not constitute
a complete release source offer. See [third-party speech notes](../THIRD_PARTY.md#managed-local-speech)
for exact revisions, source links, and the separate voice conditions.

## Real model validation

Download the selected local model separately before testing.

**Test model** for recognition downloads a pinned recording from the official
Vosk examples and submits it to the selected recognizer. A local test runs on
an isolated worker without microphone capture. A Groq test makes an actual
transcription upload with the selected model and compares its spoken digits to
the recording's known phrase. File presence or an API key alone never produces
a passing result. The test recording cannot dispatch review commands, grade a
card, or populate an answer. A recognition test is bounded to 45 seconds after
asset preparation, and cloud requests have their own 30-second timeout.
[Official validation recording](https://github.com/alphacep/vosk-api/tree/v0.3.45/python/example)

The official recording is scored as literal words for Vosk and as an exact
sequence of spoken digits for Whisper. Numeric scoring treats `oh` as zero and
expands written digit runs; its normalized reference and hypothesis appear in
the result alongside the untouched model output. The balanced Vosk profile
produced an exact phrase match on three consecutive runs of that recording on
the development machine, with no grammar or vocabulary hints. This is a fixture result, not a
guarantee for arbitrary microphones or speech. The validation result
distinguishes an exact phrase match from a completed transcription
whose words differ, and retains the transcript and word-level error counts so
model quality is visible rather than reported as an opaque pass or failure.

### Custom recognition tests

Speech recognition settings also provide a custom test set for recordings that
reflect the user's microphone, accent, vocabulary, and review commands. Five persistent slots are shown from the start. Import or record a short PCM
WAV in any slot, enter its expected transcript in the adjacent field, and use
the right-hand **Test slot** action or the bottom-right **Test custom set**
action. Clearing or replacing a recording preserves the slot and its expected
text. Empty slots are skipped when testing the set. Recordings are
normalized to mono 16-bit PCM at 16 kHz, bounded to 60 seconds, and stored in
the per-user application data directory. They are independent of cards,
sync, backups, and model downloads.

The application header shows when the microphone is listening, including
voice review and custom recording. During a custom recording it offers pause,
resume, and stop/save controls that remain visible when the slot is scrolled
out of view. The active slot shows the same controls, captured duration, and
recent microphone amplitude. Pausing suspends input, excludes paused audio from
the saved sample, and freezes the activity history. The meter displays measured
PCM amplitude rather than an animated placeholder.

Expected transcripts and recognition vocabulary use labeled, bounded textboxes
with visible focus styling. Expected transcripts remain selectable but become
read-only during capture or testing. Empty slots say **Need recording**. The
recognition/output tabs stay above the scrolling settings content and restore
each tab's own scroll position.

Each result retains the raw transcript and reports word error rate, accuracy,
substitutions, deletions, and insertions. A completed result can therefore be
useful even when it is not an exact match. Changing the selected provider,
model, path, acceleration, vocabulary, correction configuration, or credential
cancels an active set and invalidates its earlier scores. Custom testing does not
download model files automatically and shares the normal speech worker so it
does not keep another recognition model loaded.

### Vocabulary and transcript correction

**Recognition vocabulary** supplies optional project terms to local Whisper.
It is context rather than a restricted dictionary, and can make recognition
worse as well as better. Leave it empty for an unprompted comparison. The
built-in fixture and custom expected transcript are never provided to the
recognizer or correction model.

**Correct transcripts with language model** is optional and off by default.
The Signal path diagram beside the setting shows speech, recognition,
language-model correction, and the resulting text. Enabling correction
highlights the language-model icon and routes through it. With correction
off, the icon is muted and a curved path bypasses that stage to deliver
recognized text directly. Each step has a tooltip explaining its role.
It uses the provider selected in Language model settings. Local correction
requires the already downloaded model and never downloads weights during
correction. Cloud correction uses the configured endpoint, model identifier,
and provider key; it sends the raw transcript and optional vocabulary as text.
It never sends the expected test transcript, card answers, or audio to the
language model. There is no automatic fallback between local and cloud
providers. It requests conservative grammar, spelling and acronym corrections
using the optional vocabulary. Raw text is retained; invalid structured output or changed numeric
values is rejected with a visible error and the raw transcript is preserved.
Commands are interpreted from raw speech and bypass correction. Correction
output is treated as answer text and cannot create a review command.

Custom tests show the recognition output and score separately from the
corrected output and score. The correction model receives only the raw
transcript and vocabulary, never the expected test text or card answers. These
scores measure recognition and correction independently; a correction is not
proof that a technical abbreviation was understood correctly.

**Test model** for local reading synthesizes a short phrase, checks that samples
are finite, in range, non-silent, and have a plausible length, then plays it
through the default Qt audio output. It passes when playback completes. A test
needs an available output device; generating a buffer alone is insufficient.
System-engine tests observe actual synthesis/playback state through Qt. The
status confirms software playback, while the user can listen to assess voice
quality and audibility. Reading is canceled cooperatively and generated audio
is discarded after playback or cancellation.

## Cloud and system alternatives

Select **Remote (cloud-provider)** in Settings > Voice > Speech recognition
and add a Groq API key when cloud recognition is
desired. The default cloud model is `whisper-large-v3-turbo`; requests use Groq's
HTTPS transcription endpoint and a JSON response.
[Groq speech documentation](https://console.groq.com/docs/speech-to-text)

Fedora stores the key through `secret-tool` in the desktop Secret Service
keyring when available. If storage is unavailable or locked, the key remains
usable for the current session and Settings shows that status. `GROQ_API_KEY`
is an optional development alternative. QML only sees key availability and
storage status. Keys stay out of application settings, SQLite collections,
backups, logs, sync payloads, and process arguments.

Speech output also offers **Remote (cloud-provider)**. Configure the speech
endpoint, model, voice, and a separate speech-output API key. The defaults use
Groq's `canopylabs/orpheus-v1-english` model with the `troy` voice. Compatible
speech endpoints must accept the same JSON speech request and return PCM WAV audio.
Long prompts are split into smaller requests to respect Groq's documented
200-character input limit.
[Groq speech-output documentation](https://console.groq.com/docs/text-to-speech/orpheus)

The remote connection sends the text being read to the selected service only
when speech or its explicit model test is requested. Selecting Remote or
opening Settings does not start a request. The speech-output key uses its own
Secret Service entry; it is not automatically copied from recognition.
`GROQ_TTS_API_KEY` is an optional development alternative. There is no automatic
fallback to another speech provider. Remote audio uses the same default output
device and microphone suspension as local playback.

Under Local, the **System speech engine** option uses Qt's selected platform engine. Fedora
requires a working Speech Dispatcher engine and audio output for that option;
Android uses the configured Android system engine.
[Qt speech API](https://doc.qt.io/qt-6/qtexttospeech.html)

## Android status

Android is still a draft. The compact profile is selected as its intended
default, but managed Vosk and Piper runtime download packages in this change
are Linux x86-64 packages. They are explicitly rejected on Android and other
unsupported targets. An Android kit must package compatible native libraries,
then a device must verify microphone permissions, output, memory use,
background behavior, and lifecycle. The current Groq route and system speech
are alternatives for that draft; Android credentials remain in memory until
encrypted platform storage is implemented. Do not claim offline Android
speech works from the profile selector alone.

## Review commands

| Say | Action |
| --- | --- |
| `start review` | Start the selected deck. |
| `start review of Human Biology` | Start a deck by its complete name. |
| Your answer | Record the spoken answer for the current card and reveal its answer. |
| `show answer` | Reveal and read the answer. |
| `missed` or `again` | Grade as missed. |
| `partial` | Grade with half recall. |
| `partial two of three` or `two of three` | Grade with the stated recall fraction. |
| `hard`, `good`, or `easy` | Apply the stated recall grade. |
| `next card` or `previous card` | Select the adjacent card in the session. |
| `confirm rating` | Confirm a correction to a previously visited card's rating. |
| `keep rating` or `cancel rating` | Dismiss the rating correction. |
| `defer` or `skip` | Move the card to the queue's end. |
| `postpone fourteen days` | Move the review date forward by the stated number of days. |
| `postpone one week` | Postpone for seven days. |
| `repeat` | Read the currently visible question or answer. |
| `pause` | Pause review while retaining command listening. |
| `resume` | Resume the paused review. |
| `end review` or `stop` | End the review session. |
| `summarize remaining cards` | Request and read the configured language model's deck summary. |

The prefix `flashcard` is optional for every command. Commands must match the whole utterance; words such as “good” or “partial” inside an answer stay part of that answer. Partial recall accepts digit or English number forms from zero to one hundred, with a positive denominator and recalled points no greater than total points. Postponement accepts days or weeks within 3650 days. Answers are not semantically graded: the user chooses a grade after hearing the answer.

Grading advances to the next pending card. Say `previous card` to revisit a
result. Corrections to a revisited card ask for confirmation and preserve the
original review time. The final card stays open; say `end review` to finish.

Audio capture, sample conversion, voice activity detection, and local decoding run on a dedicated worker thread. Native microphone formats are downmixed and resampled to mono 16 kHz, 16-bit PCM. Leading silence uses at most 200 ms of pre-roll; audio shorter than 80 ms of detected speech is discarded. An utterance ends after 900 ms of silence or at 60 seconds. Groq requests are asynchronous, have a 30-second transfer timeout, and reject redirects. Audio is held in memory for the active utterance and request, then discarded.

The recorder stops and discards buffered audio before speech playback starts. It resumes 350 ms after speech finishes to keep the spoken prompt out of recognition. The microphone is inactive during transcription. Disabling voice, changing provider or model, and changing the active card cancel pending capture and transcription; stale transcripts cannot control another card. Speech and provider failures surface a stable `VOICE_*` or `GROQ_*` error code and a recovery action. The prototype uses basic audio energy gating; noisy environments and Bluetooth microphones still need physical-device testing and tuning.

For deterministic command testing, `VoiceController::processTranscript()` passes supplied text through the same command and answer dispatch used by recognition. This exercises review behavior without microphone or provider access. Voice tests also use a delayed mock HTTP manager to verify cancellation and response handling; they do not establish that a live Groq key or physical microphone works.
