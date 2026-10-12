# Voice settings tab studies

The selected application style is **01 Ledger tabs**. Its enclosed tab rail,
raised active tab, and heavier underline distinguish recognition from speech
output. The voice-enable switch sits beside the settings heading.

The voice settings page now uses `VoiceSettingsTabs`, a reusable keyboard-accessible tab strip. Three treatments are available in `VoiceTabsGallery`:

1. **Ledger tabs** use a quiet surface, a shared rule, and a strong active underline. This is the default because it fits the existing settings language.
2. **Segmented selector** uses two pill-like segments. It has the strongest control affordance and the shortest labels at narrow widths.
3. **Voice rail** uses larger ear and mouth-with-sound icons, bordered cells, and the full section names. It is the most discoverable treatment for first-time users.

The gallery remains available for visual comparison and keyboard navigation.

Custom speech recognition tests accept up to five user recordings, imported as
PCM WAV or captured from the microphone, each up to 60 seconds. Each row keeps
its expected transcript draft attached to the recording ID. Adding a file or
recording does not invoke inference; `Test custom set` transcribes the set.
Results show the actual transcript, word error rate, derived word accuracy,
substitutions, omissions, and insertions. Imperfect transcripts retain their
scores. The built-in test distinguishes a transcript mismatch from a model or
runtime failure.
