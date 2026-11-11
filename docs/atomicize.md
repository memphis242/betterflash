# Atomicize a saved card

Atomicize is an optional library action. It uses the local model selected in
Language model settings by default, or an explicitly configured remote
endpoint, model, and key. Opening the dialog previews the saved source without
calling the model. Choose Generate proposal to send the selected saved card's
front, back, kind, and answer-point count to that provider. It sends no other
cards, review history, credentials in content, or image pixels. For a cloze
source, the same front is also supplied with its cloze answers filled in so the
model can read the complete prose alongside the original recall markup.

The provider first decides whether the card should stay together. Ordered
steps, procedures, comparisons, and closely related bullet lists are
explicitly treated as candidates for keeping one recall unit. Several bullets
alone do not justify a split. The requested benefit is distinct learning
objectives with more targeted questions. Every candidate, including optional
candidates alongside a keep recommendation, must stand alone. The prompt requires
an explicit subject and scope in each front, concrete nouns in place of dangling
pronouns, and an isolation check of questions, answers, cloze text, and hints.
Repeating the source-grounded noun is preferred to ambiguous references. Literal
code is preserved, and missing context must not be filled with invented facts.
The prompt distinguishes a hidden recall term or hint from a sentence's subject
and requires checking each cloze question with its answers hidden. For example,
"[cloze1:Durability] means it survives restart" needs the source-grounded noun
"a transaction" in place of "it"; numbering or hints do not repair that context.

If a split is useful, the proposal contains two through five basic or cloze
cards. There are no recursive requests and no automatic follow-up splits. A
keep result has a reason and may include optional replacement candidates. The
recommendation remains visible while those candidates are reviewed. This limits growth per action;
real examples and user review will guide further prompt refinement.

## Manual replacement sets

Create split manually opens two blank replacement drafts using the saved source
as a reference, without requiring a model, API key, or generation request. The
source remains accessible and selectable in its own tab. Add card also appends a
blank draft to an existing model proposal, so the user can include concepts the
model omitted. **Add editable copy** on the Source tab adds a replacement draft
with the original front, back, and partial answer points, ready to trim or edit.
The source snapshot remains unchanged and the named Replace action removes the
original along with creating the reviewed replacements. Remove card removes
only an unsaved draft; it never deletes a saved card.

Model output stays limited to two through five candidates. A user-edited or
entirely manual replacement set may contain two through twenty cards. The card
navigation scrolls horizontally when needed. At least two drafts are required
for Replace; basic cards need a front and back, while cloze cards may have an
empty back. Manual drafts use the same cloze normalization, media preservation,
stale-source checks, and transactional save as generated proposals. Preparing a
manual split is independent of the model request's source-length limit.

## Review and save

Generate Proposal spans the top of the dialog. While the model is running, a
prominent status message and an indeterminate progress bar show that work is
in progress. The bar does not imply a completion percentage from the provider.
The completed recommendation and its explanation share a distinct panel directly
below the generation button, with a bold heading and bounded explanation. The
provider disclosure is available in the generation button's hover tooltip and
accessible description instead of repeating in the dialog body.

Valid cloze markers are normalized in application code, independently of the
model's numbering. Each replacement starts at group 1, with consecutive group
numbers in ascending order of their original group numbers. Repeated markers
from the same group remain together, and answers, hints, and escapes are
preserved. The proposal preview renders clozes immediately. Normalization runs
again when edited proposals are applied, and cloze cards receive their own
group review schedules when saved. Cloze markers belong on the front, with
optional supporting text on the back. Back-side markers or malformed marker
syntax are rejected with a correction message. Existing saved cards are not
rewritten automatically.

The bounded preview dialog keeps the original available in a Source tab. Each
proposed card has editable Markdown question and answer fields and an answer-
point count. Related multi-point answers can stay grouped and retain partial
grading. Images must use the original card's collection references; proposals
cannot invent attachments or omit the existing ones.

Only the named Replace action changes the collection. It creates fresh, due
review items for every direction or cloze group of the new cards and deletes
the original source note in one SQLite transaction with its outgoing sync
events. Historical reviews of the original remain in history. The preview
stays open until storage confirms the transaction. Canceling a provider
request uses the Cancel request button directly below Generate Proposal; it
leaves the original unchanged and keeps the dialog open. Close and the header
close control are faded and disabled while generation or saving is in progress.
Saving replacements cannot be canceled. A failed
request exposes Try again; a keep decision can be regenerated with Generate
again. Closing the dialog discards unsaved proposals. Writable proposals
require an explicit close or named action.

The replacement transaction is local to the device. Sync carries the deletion
and new cards as separate events, so another device can temporarily see part
of the replacement if a sync is interrupted between pages. A completed sync
carries all of the saved changes.

If the source is edited or deleted while the proposal is being prepared,
saving the split is rejected. A failed write rolls back the entire
replacement. Keep the edited proposal open and resolve the reported cause. For
a changed source, close and reopen Atomicize to preview its current content
before generating or manually preparing replacements again. Both workflows use
the source snapshot shown in the dialog.

## Errors and retry instructions

Failures appear at the top of the dialog with a red boundary, a plain-language
summary, and a suggested next action. Technical codes and causes are available
under Show details, collapsed by default. When the model recommends keeping the
original but also supplies replacement candidates, the dialog displays that
recommendation and allows inspection and editing of the optional candidates.
They pass the same validation and normalization as a split recommendation. Only
the explicit Replace action changes the original.

Edit instructions opens an optional editor for guidance added to the next model
request. Instructions are limited to 4000 characters, remain available across
retries in the current dialog, and are sent only when Generate Proposal or Try
again is clicked. They do not override structural validation or storage safety.
Closing Atomicize discards these instructions. No automatic retry calls occur.

The original-only preview uses a Source heading separated by a horizontal rule.
Source becomes a navigation tab once replacement drafts are available. Ongoing
progress messages end with three dots.

## Model boundaries

The request uses chat completions with JSON object mode. The local model and
remote provider must support that mode. Provider output is checked for types,
bounds, duplicate questions, image references, and a maximum of five cards.
Whole-number counts represented as JSON numbers or numeric strings are
accepted, but missing, fractional, and out-of-range counts are rejected with
the affected proposal identified. These checks do not determine whether the
meaning is correct or whether closely related concepts belong together; that
remains the purpose of the editable review step. Automated verification uses
mocked keep and split responses. Live model quality requires the user's
examples and key.

Automated GUI coverage is currently paused. When resumed, update the Atomicize
sweep to invoke Generate proposal explicitly before waiting for a response.

Each candidate also offers **Check correctness** and **Check completeness**
before replacement. These inspect the current unsaved candidate using the
configured model, with optional Tavily evidence. See [Card checks](card-checks.md).
