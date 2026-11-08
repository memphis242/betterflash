# Adaptive v1 scheduling

Each review item has its own due timestamp, review count, stability in days, and
difficulty between 0 and 1. A basic note has one item. A reverse note has two.
Each distinct numbered cloze group has an item, even when the same group occurs
several times in a sentence. Editing a note preserves schedules for surviving
directions or groups; new groups start due immediately.

The editor's inverted-form option uses the reverse direction. Its question is
the exact prefix "Ask the question that this answers based on deck context: "
followed by the original answer. Its answer is the original question. The prefix
is applied during rendering, so editing and saving a note cannot accumulate it.
Existing forward and reverse schedules keep their identifiers when edited.

Adaptive v1 is a small, inspectable heuristic. It has not been fitted to a
retention dataset. Its persisted review history allows a future calibrated
scheduler to replay the same grades and timing information.

## Short learning steps

New directions are due immediately. Their first graded review schedules another
review in 10 minutes, regardless of grade. Subsequent short learning steps are
10 minutes, 1 hour, 8 hours, and 1 day:

- Good and Easy advance one step.
- Partial and Hard repeat the current step.
- Missed returns a direction still in these sub-day steps to 10 minutes.

The interval is measured from the saved review timestamp, not the start of a
calendar day. Difficulty still responds to grades during learning; response
time and recalled fraction do not change these fixed learning intervals.
Reaching 1 day graduates the direction to adaptive daily scheduling. Graduation
is stored explicitly with the schedule and travels through sync and backups.

Daily schedules retain at least one day of stability. A miss on an established
direction retains 25% of its stability, with a one-day floor: 30 days becomes
7.5 days of stability and a 7-day interval, 8 days becomes 2 days, and 1 day
remains 1 day. It does not restart the short learning sequence.

Review sessions use a fixed queue. Finish the session and start another review
to pick up directions that have become due again; they do not automatically
reappear within an active session. Due counts refresh as time passes, without
restarting searches, discarding loaded card history, or changing the active
review. Clock changes are checked at least every minute while cards are loaded.

## Recall and timing

The five grades are Missed, Partial, Hard, Good, and Easy. Partial recall records
the number of recalled points divided by the note's answer-point count; a spoken
fraction such as "two of three" records the same value. Other successful grades
default to full recall. The scheduler also accepts an explicit fraction.

The response clock starts when the question is shown and stops when the answer
is revealed. Paused time and time spent choosing a grade are excluded. Deferring
an item gives it a new response clock when it returns.

For a prompt with `w` whitespace-separated words, `m` dollar delimiters, and `c`
lines when fenced code is present, the reading allowance in seconds is:

```text
allowance = clamp(4 + w / 3 + 1.5 * m + 0.75 * c, 4, 180)
timing = clamp(allowance / max(allowance, response_seconds), 0.7, 1)
difficulty_factor = 1.15 - 0.3 * old_difficulty
```

An answer given within its reading allowance has no timing penalty. A slower
answer reduces interval growth by at most 30 percent. Answer speed never earns
an extra bonus. Long prompts, code, and math receive more reading time.

## Daily stability update

Let `s` be old stability, `d` old difficulty, and `r` recall fraction:

| Grade | New stability | New difficulty |
| --- | --- | --- |
| Missed | `max(1, 0.25*s)` | `d + 0.16` |
| Partial | `s * (0.65 + 0.45*r) * timing * difficulty_factor` | `d + 0.07 - 0.06*r` |
| Hard | `s * (1.05 + 0.30*r) * timing * difficulty_factor` | `d + 0.03 - 0.04*r` |
| Good | `s * (1.35 + 0.55*r) * timing * difficulty_factor` | `d - 0.035 - 0.035*r` |
| Easy | `s * (1.70 + 0.80*r) * timing * difficulty_factor` | `d - 0.07 - 0.05*r` |

Difficulty is clamped to `[0, 1]`. Daily stability is clamped to `[1, 3650]`
days; its interval is the floor of that value in days. Short learning intervals
have exact fractional-day stability, with a minimum of `1/144` day (10 minutes).
Reviews are saved atomically with the changed schedule. Selecting a grade advances
to the next pending card after the commit. Previous/next navigation also remains
available independently of grading.

## Correcting a rating

Returning to a graded card and choosing a different grade or recall fraction
opens a confirmation. When all items are graded, the final card stays open until
Finish review; its rating can still be adjusted.
The correction updates the same review, preserving its original timestamp and
response duration. It recalculates stability, difficulty, and due date from the
schedule before that review, using the corrected grade. The due date is anchored
to the original review timestamp. The review count is not incremented again.
Corrections are available during the active session, including after every item
has been graded; Finish review ends that session.
The original pre-review phase also determines whether a correction uses a
learning step or a proportional daily adjustment. A corrected short interval
may already be due if enough time has passed since the original review.

## Storage and compatibility

Schema 8 accepts stability down to 10 minutes and preserves existing schedules,
history, and pending sync operations during migration. Migration does not
change existing due dates or stability. Previously reviewed directions are
marked graduated because their scheduler used daily intervals, including those
with fractional-day stored stability. Legacy backups and sync events without a
graduation field use the same rule. Their next grade uses daily scheduling;
they cannot be mistaken for directions still learning. Unreviewed directions
enter the 10-minute step after their first grade. A learning schedule between
short steps is treated as the next allowed step before applying a grade.
Conflicting offline learning reviews cannot reverse graduation. The server
retains the graduated schedule and greatest observed review count, preserves
both review records, and reports the affected card in retained sync notices.
Explicit reset, fresh direction creation, and applicable undo or rating
correction operations can deliberately restore a learning state.
Portable backup version 6 and sync protocol 4 support short intervals. Upgrade
the server and every device together; older clients cannot read these schedules.

## Deferral and postponement

Defer moves the current review item to the end of the active queue. It writes
neither a grade nor a different due timestamp. It can be used repeatedly.

Postpone removes the current review item from the queue and sets its due date
to the start of the selected future local day, stored as UTC. It keeps the
item's stability, difficulty, and review count. Other directions or cloze groups
remain independently due. Postponement is synchronized to other devices.
