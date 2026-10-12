# Learning overview (provisional)

History includes a calendar of review activity and a numerical summary. Its
default scope is the entire library. The filter can select multiple decks and
their children without changing the deck selected on Review or Library. A
selected parent and child are treated as one union: cards, variants, reviews,
and activity days are counted once. Empty selection means the entire library.
The deck picker uses checkbox selection. Clicking a checkbox toggles that deck;
Shift-clicking selects the range from the last checkbox click, using the
checked state of the anchor for the range. The Entire library option, or no
explicit deck selection, means the whole library.

## Thirty-day summary

The current window includes today and the preceding 29 local calendar dates.
The comparison window is the 30 dates immediately before that. Each review
variant uses its latest recorded result within a window, so repeatedly answering
one easy item does not give it extra weight in the score.

- **Practice score:** within each deck that directly contains current review
  variants, sum the recalled fractions of variants reviewed in the current
  window and divide by the deck's total current variant count. Unreviewed
  variants contribute zero. Average these deck scores with equal weight and
  multiply by 100. Empty decks and folders do not enter the denominator.
- **Recall:** the average recalled fraction among current variants reviewed in
  the window. Its color follows their average rating. This is separate from the
  practice score, which accounts for unreviewed material.
- **Decks reached:** decks with at least one reviewed current variant, out of all
  decks in scope that directly contain variants.
- **Active days:** local dates with at least one recorded review, out of 30.
- **Recall trend:** the current observed recall minus the previous window's
  observed recall, expressed in percentage points. No comparison is shown until
  both windows contain results. The two windows may cover different material.

No eligible cards produces an unavailable score. Eligible but unreviewed cards
produce a zero practice score and unavailable observed recall. Reverse directions
and cloze deletions are separate review variants. Partial recall contributes its
recorded fraction. Correcting a rating updates its original result and date.

This is a provisional measure of recent practice and coverage, not an estimate
of a person's intelligence or a calibrated prediction of long-term retention.
The formula, window, and presentation can be revised after use.

## Calendar and deck scope

Heatmaps count saved review answers on each local calendar date, across the full
retained history. Their color intensity represents activity, not correctness.
Daily tooltips also report average recall. Future dates do not add activity.
The full calendar supports year navigation; the compact deck-pane view shows
the most recent 13 weeks. The Heatmap action on Review and Library opens the
full calendar for the selected deck and its children.

Deck calendars and scores follow the cards currently in that deck's subtree.
Moving a card carries its review history into its new scope. Deleted cards'
history remains in the library-wide activity calendar, but is excluded from
current-card scores and deck calendars. Resetting schedules preserves saved
review history. Changes to deck membership can therefore change the summary.

Calendar aggregation and scoring run on the database worker. Views request their
own scope and ignore stale responses when the scope changes. No provider or
language-model request is needed.
