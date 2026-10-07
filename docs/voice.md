# Voice review

In Settings > Voice, add a Groq API key, choose Groq, and enable voice. Voice is always disabled at application startup. No local speech model is needed for Groq. The default model is `whisper-large-v3-turbo`; the implementation uses Groq's HTTPS transcription endpoint and JSON response. [Groq speech documentation](https://console.groq.com/docs/speech-to-text)

Fedora stores the key through `secret-tool` in the desktop Secret Service keyring when available. If storage is unavailable or locked, the key remains usable for the current session and the settings show that status. `GROQ_API_KEY` is an optional development alternative. Keys are readable only by native C++ code; the QML controller exposes availability and storage status. Keys are never saved to application settings or passed in process arguments. The Android draft keeps keys in memory until encrypted Android storage is added.

The prototype reads prompts using the system text to speech engine through Qt. Fedora requires a working Speech Dispatcher engine and audio output; Android uses the selected system speech engine. Text to speech provider selection and a keyed speech synthesis service can be added after choosing a provider. [Qt system speech API](https://doc.qt.io/qt-6/qtexttospeech.html)

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

Grading leaves the current card open. Say `next card` to continue. Corrections to
a revisited card ask for confirmation and preserve the original review time.

Audio capture, sample conversion, voice activity detection, and optional local decoding run on a dedicated worker thread. Native microphone formats are downmixed and resampled to mono 16 kHz, 16-bit PCM. Leading silence uses at most 200 ms of pre-roll; audio shorter than 80 ms of detected speech is discarded. An utterance ends after 900 ms of silence or at 60 seconds. Groq requests are asynchronous, have a 30-second transfer timeout, and reject redirects. Audio is held in memory for the active utterance and request, then discarded.

The recorder stops and discards buffered audio before system speech starts. It resumes 350 ms after speech finishes to keep the spoken prompt out of recognition. The microphone is inactive during transcription. Disabling voice, changing provider or model, and changing the active card cancel pending capture and transcription; stale transcripts cannot control another card. System speech and provider failures surface a stable `VOICE_*` or `GROQ_*` error code and a recovery action. The prototype uses basic audio energy gating; noisy environments and Bluetooth microphones still need physical-device testing and tuning.

For deterministic command testing, `VoiceController::processTranscript()` passes supplied text through the same command and answer dispatch used by recognition. This exercises review behavior without microphone or provider access. Voice tests also use a delayed mock HTTP manager to verify cancellation and response handling; they do not establish that a live Groq key or physical microphone works.

The optional local test provider dynamically loads Vosk without a link time dependency. Download its model and native library only when wanted:

```sh
python3 scripts/setup-voice.py --vosk --directory /workspace/betterflash-models/voice
```

Choose “Local test” and set the model path to `/workspace/betterflash-models/voice/model`. The setup script fetches a pinned small English model and Vosk 0.3.45 native wheel, verifies the wheel's PyPI SHA-256, rejects archive traversal, and records download hashes alongside the assets. Existing installations are preserved. A known model hash can be supplied with `--model-sha256`. [Vosk model source](https://alphacephei.com/vosk/models), [Vosk library release](https://pypi.org/project/vosk/0.3.45/)
