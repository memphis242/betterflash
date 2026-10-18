# Study guidance

BetterFlash ranks study priority deterministically from saved review results.
The score is a provisional heuristic, not a probability of mastery or a diagnosis
of missing foundational knowledge. No model is needed to calculate it.

## Using it

- Library shows outlined Watch, Fragile and Needs study badges on weak cards.
  Enable **Weakest first** to rank a deck or the whole library. Search result
  groups retain their relevance order; weakness sorts only inside each group.
- Open a card's detail and choose **Study guidance** for the reasons and a
  suggested independent study step.
- During Review the badge and card outline describe the exact active review
  direction. **Study guidance** opens the same explanation and pauses the timer;
  closing it returns to that card and resumes a review that was running.
- The Review page's **Review order** selector defaults to **Weakest first**.
  It orders only cards already eligible for that review, including new cards.
  **Due date** restores the ordinary schedule ordering. The choice applies at
  session start and persists locally; it does not alter any due date.
- **Ask language model** is optional. It uses the configured local or remote
  language model to suggest an explanation, study steps and a self-check.
  Opening the dialog never starts inference. The dialog identifies the model
  and, for a remote model, the endpoint receiving the card data.

## Evidence and independent directions

Each forward, reverse and numbered cloze variant is assessed independently.
Library uses the highest score among assessed directions and names that direction
in the guidance dialog. An unreviewed direction does not inherit another
direction's history or imply weakness. Cards with no observed reviews are
**Not assessed**. Fewer than three observed reviews are marked **early** and
provisional, including a single missed card.

The model uses up to the latest twelve available observations, ordered by review
timestamp and then review ID. The current schedule's review counter bounds that
history. Resetting a card or deck makes its directions unassessed; retained
historical events remain visible in the timeline. Undoing a reset restores the
schedule counter and the associated evidence. Reviews of deleted cards or
removed directions do not enter live rankings.

The displayed review count comes from the current schedule. Evidence samples
show how many retained observations actually contributed, which can be lower
than that counter for imported or incomplete history. Old observations do not
change merely because the app is left open. Overdue status is handled by the
scheduler, separately from measured weakness.

## First scoring formula

For newest-first observation index `i`, weight is `0.82^i`. Ratings run from
Missed `0` through Easy `4`.

- **Recall loss:** weighted mean of
  `0.65 * (1 - recalled fraction) + 0.35 * (1 - rating / 4)`.
  Partial recall contributes its actual recalled fraction.
- **Difficulty:** weighted proportion of Hard, Partial and Missed ratings.
- **Deterioration:** positive difference between mean recall loss in the newer
  half and older half of the sample. Requires at least six observations.
- **Response speed:** a small additional signal after at least three
  positive-duration successful responses to the same direction. The latest
  successful time is compared to its personal successful median, with allowance
  `max(1 second, 10% of estimated question reading time, 15% of median time)`.
  Its normalized excess contributes up to ten points. The timer already excludes
  paused time; this remains an approximate signal for complicated problems.
  Missed and Partial answers do not establish a speed baseline.

`score = clamp(70 * recall loss + 20 * difficulty + 10 * deterioration + speed points, 0, 100)`.

| Score | Tier |
| --- | --- |
| Below 25 | Steady |
| 25 to below 50 | Watch |
| 50 to below 75 | Fragile |
| 75 and above | Needs study |

Repeated full-recall Hard results reach Watch. Repeated half-recall Partial
results reach Fragile. Misses reach Needs study. Speed by itself cannot promote
otherwise perfect Good or Easy responses into a weak tier. These thresholds
are deliberately exposed for refinement during ordinary use.

Weakest-first review order compares finite scores, then retains the existing due
ordering for exact ties. Unassessed directions and assessed zero-score directions
keep their due-order tie. The order stays fixed during the session; grading,
queue-end deferral, selecting another card and rating corrections retain their
existing behavior. Weakness indicators refresh after committed grades,
corrections, edits, resets, sync and undo/redo, without reordering the active run.

## Model assistance and data handling

Assistance sends only the selected direction's question and answer, deck name,
card kind, tags, point count and deterministic weakness evidence. It does not send
other cards, recordings, API keys or the full review history. Card material is
marked as untrusted data in the prompt. Output is an advisory Markdown response;
it cannot change cards, schedules or grades.

Inputs are bounded by the selected local profile or remote request budget.
Oversized cards produce an actionable error instead of silently dropping part
of the question. Closing or canceling the dialog cancels an active request.
Changing relevant card content, evidence, deck context, model settings or remote
credentials invalidates the result. Card-level requests also invalidate when a
different direction becomes the weakest. Explanations and examples should be
checked against the user's study sources; model output is not verified proof.

## Implementation and verification status

Calculations and database reads run on the database worker. A temporary
transactional invalidation table tracks affected directions across all writes,
including sync and undo. Trigger inserts explicitly exclude already-dirty IDs
so duplicate invalidations remain safe inside an enclosing upsert. The initial snapshot populates the cache; later
refreshes recompute only changed directions. An indexed, correlated query
retrieves at most twelve rows per changed direction and does not scan its
complete history. Scores are derived data, with no collection schema
version change, sync payload or additional model dependency. Review-order
settings participate in the ordinary five-action undo history.

Automated testing remains paused. The changes receive compilation and source
review before manual use. When testing resumes, cover independent directions,
partial grades, sparse and incomplete history, zero-duration responses,
corrections, repeated invalidation during grading/upserts, deletion/atomicization,
reset and undo/redo, stable due eligibility,
and stale/canceled local and remote responses.

Deck-level topic grouping, rankings of completed review runs, automatically
generated remedial cards and validated generated problems remain future work.
