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

## Generate like this

Available from the card detail view, card action menus and the active review
card: **Generate like this** opens a preview of the source. Press **Generate
card** to ask the configured local or remote language model for one new basic
card targeting the same concept with a different question, example or numbers.
The request includes the front, back, deck name, tags and answer-point count.
It does not include recordings, schedules, other cards or credentials.

The model is instructed to solve the new problem independently and preserve
Markdown, code and LaTeX. The response must pass strict field, size, media and
duplicate-question checks. A model may decline when the source cannot be varied
responsibly. These checks do not verify factual or mathematical correctness.

The result appears as rendered Markdown. **Generate another** replaces the
unsaved suggestion. **Edit and save draft** opens the regular card editor with
the source deck and tags preselected; only **Save card** persists it. The source
card and its history stay intact. The new card gets a fresh schedule and normal
undo support. Custom narration is not copied to the new text.

Generation pauses an active review and preserves its current card. Closing the
generation/editor/detail workflow resumes only a review that was running when
the workflow opened, on the same review direction. An already paused review
stays paused. Closing the generation window cancels its outstanding request.
Source edits or removal invalidate the suggestion.

Follow-ups: independent answer verification, quantity and difficulty controls.
