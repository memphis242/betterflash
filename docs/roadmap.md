# Roadmap

## Agreed sequence

Recorded 2026-10-17:

1. Address the deferred desktop work below and refine the application.
2. Spend a week using the desktop application and make any needed refinements.
3. When requested, add thorough behavioral tests to preserve the accepted
   baseline and conduct focused bug hunts. Testing remains paused until then.
4. Complete Android, then Windows, then iOS support.

The desktop list is an inventory, not an instruction to implement every item
at once. Feature details and priorities remain to be agreed as work resumes.

## Completed desktop follow-ups

- **Collection recovery:** unreadable cards remain visible with a repair action;
  healthy cards remain available for review. Derived-view failures produce
  warnings, repairs preserve recovery copies, and unsafe portable exports stop
  with a record-specific error. See [collection recovery](collection-recovery.md).
- **Cloze markers and preview:** numbered `[clozeN:answer]` markers with optional
  hints and escaped content. Previews distinguish hidden spans with tint and
  italic text; formulas and code retain their layout. Older marker forms are
  unsupported. See [cloze markers](cloze-markers.md).
- **Library scope:** choose the selected deck only, the deck and its subdecks,
  or the entire collection. The scope also limits search candidates.
- **Library card creation:** saved-card previews offer **Add another card** in
  the same deck. Generated drafts use the same preview after acceptance.
- **Light-mode accent:** pastel teal action backgrounds with contrasting teal
  text and outlines; dark mode retains its apricot accent.
- **Library creation buttons:** root decks, subdecks and cards have distinct
  creation icons and consistent primary action backgrounds.
- **Deck pickers:** new-card destinations and deck parent choices use `::`
  hierarchy paths. Card destinations retain their record ID when choices reorder.
- **Preview editing:** saved-card previews offer **Edit card**, returning to the
  editor with the saved content and deck. Saving updates the same card and returns
  to its preview; canceling leaves the saved card intact.
- **Selectable card preview:** highlight and copy rendered front and back text
  in saved-card previews, including cloze answers.
- **Review deck key:** the deck diagram includes a visible key for its encircled
  R badge: review items are due in the deck or its subdecks, including new cards.
  Each badge also provides the due-item count in a tooltip.

## Deferred desktop work

- **Delayed startup feedback:** when startup takes more than one second, show
  a "Starting up..." dialog so the user knows the launch is in progress.
  Fast normal launches should not show it. Address repeated launch attempts
  spawning duplicate processes while startup is still underway.
- **Card line spacing:** support line-spacing formatting in cards, or at least
  increase the default spacing between lines and newlines for readability.
- **Checkpoint follow-ups:** checkpoint creation and content restoration are
  implemented, including child decks, deterministic changes, optional model
  annotations, recovery checkpoints, and portable backups. Checkpoint history
  is local to the collection; synchronizing that history remains deferred.
  Restored content uses sync events. A browser for checkpoints of entirely
  deleted root decks and larger portable-backup formats are also follow-ups.
  See [deck checkpoints](checkpoints.md).
- **Undo and returning to context:** a five-action linear undo/redo history now
  covers saved library changes, review actions, checkpoints, narration, and
  ordinary settings. Refine individual actions during manual use, then add
  behavioral coverage when testing resumes. History currently lasts for the
  application session; checkpoints provide persistent content recovery. See
  [undo and redo](undo.md). Continue refining selection, scroll, zoom, filters,
  and pane context across navigation.
- **Weak-area study guidance:** initial deterministic direction/card ranking,
  Library and Review tiers, weakest-first due reviews and explicit optional
  model guidance are implemented. Refine weights and thresholds during use;
  deck topic grouping and completed-run rankings remain follow-ups. See
  [study guidance](study-guidance.md) and [study support ideas](study-support-ideas.md).
- **Generate similar card:** one-card generation supports cloze notes and
  optional user instructions. Drafts open in the regular card editor for changes
  and preview before saving. Refine edited drafts repeatedly with optional
  instructions, then accept the current draft into the deck. Saved cards open
  a preview and are selected and revealed in the Library.
  Independent answer verification and optional difficulty controls remain
  follow-ups. See [study support notes](study-support-ideas.md).
- **Logo:** replace the current walnut mark with a deliberately drawn design.
  The user may draw it digitally or commission an artist; the current mark
  looks too AI-generated.
- **Learning statistics:** refine the provisional library/deck learning score,
  coverage, and consistency measures; account for unreviewed material rather
  than scoring only the cards already reviewed. See
  [the provisional learning overview](learning-overview.md).
- **Typography:** compare proportional and monospace text across the interface.
  Inline code already has a distinct background. See
  [GUI follow-ups](gui-follow-ups.md).
- **Voice and model refinement:** finish the hands-free behavior, model/hardware
  refinements, and usage-help ideas described below. Summaries, atomicization,
  transcript correction, and custom narration already exist; refine their real
  use rather than treating them as unimplemented features.

## Android completion

As of 2026-10-19, the desktop baseline is **v0.1.0-beta**. The user accepted
the initial study-guidance and Generate similar card features and paused the
desktop TODO sweep to plan Android. Android implementation is not yet
authorized by that planning request. Start with the
[Android handoff plan](android-plan.md), targeting a Samsung Galaxy S26 Ultra
running One UI 9.0 / Android 17. Automated testing remains paused.

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

## iOS support

iOS follows Windows. It is a future port, with no completed build or device
verification. Plan the Apple development toolchain, signing and packaging,
credential storage, model/runtime compatibility, microphone permissions, and
audio lifecycle behavior when that milestone begins.

## Later local-model work

- Add more hardware profiles and memory telemetry after testing on target
  devices.
- Evaluate additional speech runtimes and Android-compatible packages.
- Revisit GPU backends and model quantization after measuring startup, memory,
  and review latency.

## Future hands-free voice review

- Refine the existing always-on review mode and wake-word handling into a
  complete hands-free experience, with clear controls for false activations,
  background noise, microphone device changes, and lifecycle state.
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

## Baseline verification when testing resumes

- After the week of use and any refinements, add behavioral coverage and conduct
  bug hunts across the accepted desktop baseline, including persistence,
  scheduling, search, synchronization, model integration, and voice lifecycle.
  Prefer behavior over incidental text or layout assertions.
- Cover edits during recording and saving, confirmation rejection after text
  or audio changes, and confirmation followed by another edit.
- Cover legacy sync fields versus explicit clears, schema 5 to 6 migration,
  and version 5 backup round trips with per-side baselines and checkpoints.
- Cover five-minute narration capture, playback and transfer, and request-scoped
  editor saves returning to the correct card only after persistence succeeds.

## Separate server work

VPS deployment details remain deferred. HTTPS hosting, separate accounts, log
compaction, client capability negotiation, and replaying concurrent reviews into
a combined schedule are distinct infrastructure decisions. See
[the sync protocol](sync.md).
