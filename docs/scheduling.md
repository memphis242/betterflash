# Adaptive v1 scheduling

Each review item has its own due timestamp, review count, stability in days, and
difficulty between 0 and 1. A basic note has one item. A reverse note has two.
Each distinct numbered cloze group has an item, even when the same group occurs
several times in a sentence. Editing a note preserves schedules for surviving
directions or groups; new groups start due immediately.

Adaptive v1 is a small, inspectable heuristic. It has not been fitted to a
retention dataset. Its persisted review history allows a future calibrated
scheduler to replay the same grades and timing information.

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

## Stability update

Let `s` be old stability, `d` old difficulty, and `r` recall fraction:

| Grade | New stability | New difficulty |
| --- | --- | --- |
| Missed | `0.25` | `d + 0.16` |
| Partial | `s * (0.65 + 0.45*r) * timing * difficulty_factor` | `d + 0.07 - 0.06*r` |
| Hard | `s * (1.05 + 0.30*r) * timing * difficulty_factor` | `d + 0.03 - 0.04*r` |
| Good | `s * (1.35 + 0.55*r) * timing * difficulty_factor` | `d - 0.035 - 0.035*r` |
| Easy | `s * (1.70 + 0.80*r) * timing * difficulty_factor` | `d - 0.07 - 0.05*r` |

Difficulty is clamped to `[0, 1]`; stability to `[0.25, 3650]` days. The next
interval is at least one day and otherwise the floor of stability. The first
successful review is capped at two days. A missed answer returns the next day.
Reviews are saved atomically with the changed schedule before the queue advances.

## Deferral and postponement

Defer moves the current review item to the end of the active queue. It writes
neither a grade nor a different due timestamp. It can be used repeatedly.

Postpone removes the current review item from the queue and sets its due date
to the start of the selected future local day, stored as UTC. It keeps the
item's stability, difficulty, and review count. Other directions or cloze groups
remain independently due. Postponement is synchronized to other devices.
