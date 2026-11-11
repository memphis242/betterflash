# Deck vocabularies and suggested card tags

From the Library deck actions menu, choose **Manage tags / Suggest card tags**.
The **Deck vocabulary** tab accepts one tag per line, up to 100 tags of 80
characters each. Save the vocabulary to keep it with the deck. It is included
in collection export, sync, checkpoints, and undo. Changing the vocabulary does
not rename or delete tags on existing cards.

**Propose tags from deck cards** scans every card in the selected scope, one
model request per card. Include subdecks when appropriate. The scan uses the
entire front and back, existing card tags, and vocabulary to suggest additional
topical tags. Oversized or unreadable cards stop the scan with an explicit
error; no undisclosed sampling or truncation is used. Review the proposed
additions, add them to the editable vocabulary, make adjustments, and save.
A canceled or failed scan retains its completed suggestions until closed.

On the **Card tagging** tab, **Suggest tags for cards** asks the configured
model to select from the saved vocabulary for each card. The card actions menu
also offers **Suggest tags** for one saved card, using its own deck vocabulary.
An empty vocabulary must be entered or proposed and saved first. No request
runs simply from opening the dialog. Remote models receive card text; each
card uses a model request and may incur provider charges. Web search is not
used. Model suggestions can be wrong and always require review.

Select a result to inspect its card, proposed tags, and reason. Ctrl-click adds
to the selection; Shift-click selects a range. **Select all suggestions** is
available for a whole run. **Apply selected suggestions** adds tags while
preserving existing tags, card content, narration, partial answer points, and
review schedules. Cards that changed since the request cannot be applied.
The selected set is saved atomically with its sync events and one undo action;
if any selected card changed, none of the selected assignments are saved.
Find tagged cards with the cards pane's tag filter.

The operation covers a snapshot of up to 5,000 cards. Cards added or moved into
the scope after a run starts require another run. Completed results remain
available on cancellation or provider failure. Closing the dialog discards
unsaved drafts and model results. A deck vocabulary belongs to that deck;
subdecks retain their own vocabularies even when a parent run includes them.
Reserved check follow-up tags are excluded from the vocabulary.

The desktop storage schema is version 9. Opening an existing collection
migrates it transactionally; older desktop builds cannot open that migrated
SQLite file. Optional vocabulary fields in older sync events are treated as
absent and preserve the current vocabulary. Use an updated sync server to
validate the vocabulary field on incoming events.
