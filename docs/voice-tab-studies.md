# Voice settings tab studies

The voice settings page now uses `VoiceSettingsTabs`, a reusable keyboard-accessible tab strip. Three treatments are available in `VoiceTabsGallery`:

1. **Ledger tabs** use a quiet surface, a shared rule, and a strong active underline. This is the default because it fits the existing settings language.
2. **Segmented selector** uses two pill-like segments. It has the strongest control affordance and the shortest labels at narrow widths.
3. **Voice rail** uses larger ear and mouth-with-sound icons, bordered cells, and the full section names. It is the most discoverable treatment for first-time users.

The gallery is intended for visual comparison. The eventual choice should be made after checking the settings page at its narrowest supported width and with keyboard focus visible.

Custom speech recognition tests accept up to five user recordings. Each row keeps its expected transcript draft attached to the recording ID. Adding a file or recording does not invoke inference; the `Test custom set` action is the only operation that transcribes the set. A result shows the model transcript and its pass or fail status. The backend may additionally provide word error statistics for later display.
