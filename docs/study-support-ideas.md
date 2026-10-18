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

Deferred BYOK language-model idea: start from an existing problem card and
generate a similar practice card targeting the same concept. Examples include
derivatives, integrals and applications of a topic, varying the numbers,
expressions or combinations so the user can practice beyond a single example.

Working action name: **Generate like this**. The generated card should include
the new problem and its corresponding answer. Quantity, difficulty controls,
answer verification and how the user reviews and saves suggestions remain to
be designed. No generation action is implemented in this Library iteration.
