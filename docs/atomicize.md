# Atomicize a saved card

Atomicize is an optional library action. It uses the local model selected in
Language model settings by default, or an explicitly configured remote
endpoint, model, and key. Opening the dialog previews the saved source without
calling the model. Choose Generate proposal to send the selected saved card's
front, back, kind, and answer-point count to that provider. It sends no other
cards, review history, credentials in content, or image pixels.

The provider first decides whether the card should stay together. Ordered
steps, procedures, comparisons, and closely related bullet lists are
explicitly treated as candidates for keeping one recall unit. Several bullets
alone do not justify a split. The requested benefit is distinct learning
objectives with more targeted questions. Each replacement must stand alone: the prompt asks the model to
restore explicit subjects and resolve pronouns whose antecedents would be lost
when splitting, including inside cloze statements, using only source facts.

If a split is useful, the proposal contains two through five basic or cloze
cards. There are no recursive requests and no automatic follow-up splits. A
keep result has a reason and no proposed cards. This limits growth per action;
real examples and user review will guide further prompt refinement.

## Review and save

Generate Proposal spans the top of the dialog. While the model is running, a
prominent status message and an indeterminate progress bar show that work is
in progress. The bar does not imply a completion percentage from the provider.

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
request leaves the original unchanged and keeps the dialog open. A failed
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
before generating again. Generation always uses the source snapshot shown in
the dialog.

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
