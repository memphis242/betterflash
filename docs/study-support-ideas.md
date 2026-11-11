# Study support ideas

Status: original exploration recorded 2026-10-11. Deterministic card and direction
ranking, weakness badges, due-card ordering and optional card-level model guidance
are now implemented. See [study guidance](study-guidance.md) for the current
formula, interface and verification limits. The notes below retain the broader
ideas; completed-run ranking and deck-level topic grouping remain deferred.

## Identify weak areas deterministically

Rank a card, a completed review run, or a deck from greatest to least weakness.
The goal is to help the user decide what to study independently, beyond the next
scheduled flashcard review. Persistent difficulty may suggest a gap in the
underlying understanding, rather than simply a need for more repetitions.

Explore a deterministic, explainable ranking first. Candidate signals include
misses, partial recall, repeated Hard ratings, response time in the context of
prompt complexity, and whether performance is improving or deteriorating.
The first scoring formula is documented in the study-guidance guide and remains
open to refinement during use.
Account for separate reverse and cloze variants, pauses, reading time, corrections,
and sparse history before interpreting slow answers as weakness.

The result could recommend focused reading, note-taking, independent retrieval
practice or other study of the underlying topic. Preserve the distinction
between measured review performance and a hypothesis about missing foundations.

## Optional language model assistance

After the deterministic approach is established, consider whether a language
model adds value to identifying or explaining weak areas. Regardless of which
method identifies a weakness, an optional BYOK model could help the user work
through it on the spot with explanations, suggestions, questions, examples or
ideas for independent study.

Presentation is undecided. Avoid committing to a chat panel, automatic prompt,
or interruption during Library refinement. Support should stay in the context
of the selected card or topic when its interface is explored.

Related ideas already discussed include:

- Splitting cards with distinct learning objectives into atomic cards while
  preserving coherent lists and procedures.
- Carrying out user-requested app actions through a language model interface.
- Improving speech-to-text, text-to-speech and voice interaction.
- Summarizing the material still awaiting review in a deck.

The app has existing summary and atomicization integrations. Their provider
behavior still needs a separate verification phase; these notes do not imply
that those integrations have been tested.

## Recall speed and motivation

Revisit how response time and difficulty should contribute to study guidance.
The Library's best-time statistic is the fastest recorded review graded Hard,
Good or Easy. It can provide a personal time to beat, but its introduction does
not decide a weakness formula or change the spaced-repetition algorithm.

## Generate similar card

Available from card detail, card action menus and the active review card:
**Generate similar card** opens a request dialog. Choose **Similar to source**
(the default), **Easier than source**, or **Harder than source**, and optionally
add up to 2,000 characters of generation instructions. Press **Generate card** to
request one variation from the configured local or remote model. Opening the
dialog does not start inference. Difficulty is a request about the problem's
challenge relative to its source; it does not change review scheduling.
The dialog offers cancellation and retry when generation fails.

The result opens as an editable draft with the source deck and tags preselected.
Edit its content, kind, deck, tags or answer points. Add optional **Refinement
instructions**, choose the desired difficulty, then press **Generate refinement**.
Each request uses the latest edited draft, the original source card, the chosen
difficulty and these instructions, rather than an accumulating conversation.
Repeat as needed. Instructions are limited to 2,000
characters per request and clear after a successful refinement. During a request,
fields are read-only and **Cancel refinement** stops it. Refusals, invalid
responses and request failures preserve the editable draft and its instructions.

Source and draft content are separated from direct user instructions and treated
as untrusted study material. Recordings, schedules, unrelated cards and credentials
are excluded from prompts. Basic and reverse sources initially produce a basic
draft; cloze sources produce a cloze note with validated numbered groups and an
optional back. Refinement respects the current draft's card kind. Model responses
must pass field, size, cloze, media and duplicate-question checks. Managed images
must come from the source. These checks do not establish factual or mathematical
correctness; check the answer before accepting.

**Accept generated card** saves the current edited draft to the chosen deck.
Nothing is saved during generation or refinement. Saving opens a preview of the
persisted card and reveals it in the Library, clearing card searches and filters
that could hide it. The preview offers **Add another card** in the same deck
and **Edit card** to reopen the saved content and deck in the editor. Saving from
that editor updates the same card and returns to its preview. Canceling an edit
keeps the saved card. Rendered preview text can be highlighted and copied.
Ordinary new-card saves also open this preview; other existing-card edits return
to their detail. Canceling a draft leaves the collection unchanged. The source and
its history stay intact. Each accepted card has fresh schedules and normal undo
support; custom narration is not copied.

Generation pauses an active review and preserves its current card. Closing the
generation/editor/preview workflow resumes only a review that was running when
the workflow opened, on the same review direction. An already paused review
stays paused. Adding another card keeps the workflow open until its editor or
preview is closed. Source edits or removal invalidate further model requests;
the user's editable draft remains available for inspection and manual saving.

### Independent answer check

The generated-draft editor offers **Check answer**. For text-only cards, it makes
two fresh requests
to the selected model: first solve the question without seeing the proposed
answer, then compare that independent answer with the current draft. For cloze
notes, the first request replaces complete cloze markers, including hints, with
numbered blanks and omits the back. Neither request includes the source card,
review history, narration, credentials, or image pixels.

The result reports **model agrees**, **issues found**, or **uncertain**. Details
show the explanation and the independent answer when available. This is a
separate model check using the same configured model, not external fact checking
or a guarantee of correctness. If the solver cannot answer independently, the
result remains uncertain. Cards with image references on either side return
**uncertain** immediately without model requests, because the check cannot see
image pixels. Reverse-card checks cover Front to Back only; the inverted form
remains for manual review.

Edits to either side, card kind, or answer-point count make a displayed result
stale. Model configuration changes invalidate the result. Checking is optional,
cancelable, and never edits or saves the draft. Fields are read-only while a
check runs; canceling, a request failure, or an invalid response preserves the
draft. Review the content and use **Accept generated card** to save explicitly.
Automated and live-model behavioral verification remain paused until requested.

Remaining follow-up: quantity controls.
