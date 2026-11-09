# Cloze markers

The cloze syntax is `[clozeN:answer]`, where `N` is an ASCII number from 1
through 999. An optional review hint uses `[clozeN:answer|hint]`. Reusing a
number groups deletions under one review schedule; different numbers create
independently scheduled variants.

```text
The capital of [cloze1:France] is [cloze2:Paris].
```

Escape literal backslashes, brackets, and vertical bars inside an answer or
hint with `\\`, `\[`, `\]`, and `\|`. Unknown backslash escapes are retained.
Ordinary square brackets outside a marker are plain text. A literal marker-like
opening bracket inside an answer must be escaped. Nested markers, empty answers
or hints, invalid group numbers, and unclosed markers are rejected. Balanced
curly braces are ordinary answer text, including C++ aggregate initializers.

The prior `{{cN::...}}` syntax is not recognized as a cloze marker. Existing
card text is not rewritten automatically. Variant keys remain `cN`, preserving
the relationship between numbered groups and their schedules, history, and
variant identifiers when content uses the numbered syntax.

The sync server advertises the `numbered-cloze-markers` capability and requires
protocol version 5. Every syncing client must support the numbered syntax.

Editor, saved-card and detail previews show cloze answers with a tinted background
and italic text, without the marker delimiters. Formula and code previews identify
cloze-bearing content while retaining the formula or code layout. To keep large
notes responsive, highlighting is limited to 256 spans per preview; larger notes
remain readable with an explanation to split the card for highlighting.

There is no automatic conversion of older marker forms. Historical backups and
immutable sync logs containing them also require conversion or a fresh sync
collection before they can be replayed with this parser. Preserve an original
backup before any one-time repair; retain each `cN` direction and its schedule.
