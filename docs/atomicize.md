# Atomicize a saved card

Atomicize is an optional library action. It uses the endpoint, model, and key in
Language model settings. Clicking it sends the selected saved card's front,
back, kind, and answer-point count to that provider. It sends no other cards,
review history, credentials in content, or image pixels.

The provider first decides whether the card should stay together. Ordered steps,
procedures, comparisons, and closely related bullet lists are explicitly treated
as candidates for keeping one recall unit. Several bullets alone do not justify
a split. The requested benefit is distinct learning objectives with more targeted
questions.

If a split is useful, the proposal contains two through five basic cards. There
are no recursive requests and no automatic follow-up splits. A keep result has
a reason and no proposed cards. This limits growth per action; real examples and
user review will guide further prompt refinement.

## Review and save

The bounded preview dialog keeps the original available in a Source tab. Each
proposed card has editable Markdown question and answer fields and an
answer-point count. Related multi-point answers can stay grouped and retain
partial grading. Images must use the original card's collection references;
proposals cannot invent attachments or omit the existing ones.

Only the named Replace action changes the collection. It creates fresh, due
review items for the new basic cards and deletes the original source note in one
SQLite transaction with its outgoing sync events. Historical reviews of the
original remain in history. The preview stays open until storage confirms the
transaction. Canceling a provider request leaves the original unchanged.

The replacement transaction is local to the device. Sync carries the deletion
and new cards as separate events, so another device can temporarily see part of
the replacement if a sync is interrupted between pages. A completed sync carries
all of the saved changes.

If the source is edited or deleted while the proposal is being prepared, saving
the split is rejected. A failed write rolls back the entire replacement. Keep
the edited proposal open, resolve the reported cause, or request a new proposal
from the current source.

## Model boundaries

The request uses chat completions with JSON object mode. The model must support
that mode. Provider output is checked for types, bounds, duplicate questions,
image references, and a maximum of five cards. These checks do not determine
whether the meaning is correct or whether closely related concepts belong
together; that remains the purpose of the editable review step. Automated
verification uses mocked keep and split responses. Live model quality requires
the user's examples and key.
