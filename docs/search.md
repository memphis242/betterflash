# Library card search

Search is scoped to cards directly in the selected deck. It does not silently
include descendant decks. When no deck is selected, search covers every card in
the library and each result retains its owning deck in the card snapshot.

## Search controls and filters

The search buttons toggle their textboxes without clearing their queries or
results. Escape also hides the corresponding controls. Clear a query explicitly
to remove its restriction; hiding it keeps the search active.

The Decks pane's first field matches deck names with exact, prefix and fuzzy
matching. Its adjacent dropdown reveals **Search within decks**, which searches
card contents across the library through all five retrieval layers. Both fields
are AND restrictions, and an empty field imposes no restriction. A deck includes
its descendants' cards for this content search. Ancestors needed to show the
folder path remain visible as context even if their own names do not match.
Each result reports its strongest card match and matching-card count; its tooltip
distinguishes direct cards from cards in subdecks.

The Cards pane's filter control restricts tags, card type and review state.
Selected tags are OR alternatives; tag, type, state and query restrictions are
ANDed. Plain click selects one tag, Ctrl-click toggles a tag, and Shift-click
selects a range. Filters apply before retrieval, including the semantic candidate
limit, so unrelated tags cannot crowd matching cards out of the results.
Closing the filter popup preserves its selections.

## Result layers

The worker publishes the inexpensive text results first, then fuzzy results,
then semantic results. The list groups results in this order:

1. **Exact matches**: a whole field or word-bounded phrase.
2. **Prefix matches**: every query word starts a word in the card.
3. **Contains your query**: a substring within a field.
4. **Similar spelling**: bounded edit distance, including adjacent transpositions.
5. **Related meaning**: cosine similarity between sentence embeddings.

Literal and fuzzy matching use front, back and tags. Matching folds case,
diacritics and whitespace; Markdown labels and code remain searchable. Words
shorter than four characters require exact token spelling in the fuzzy layer.
Queries are limited to 256 characters. Prefix and fuzzy terms can match across
fields. A card appears once, in its earliest matching layer.

The result groups arrive without replacing stronger text matches with semantic
ones. Selection is retained by card ID when later layers arrive. Pending work
and network responses have query epochs; typing a new query, changing decks or
providers, and disabling search invalidate obsolete results. Errors leave the
text layers available and offer retry.

## Local semantic retrieval

Semantic matching is enabled by default. Settings > Search can turn it off.
Automatic profile selection chooses:

| Target | Model | Vector dimensions | Quantized model download |
| --- | --- | --- | --- |
| Fedora desktop | BGE base English v1.5 | 768 | 110 MB |
| Android draft | BGE small English v1.5 | 384 | 34 MB |

Each model also has a roughly 232 KB vocabulary. Both profiles can be selected
manually on either target. They are English models; multilingual model choices
remain future work. The BGE models use CLS pooling, normalized vectors and the
recommended retrieval query prefix. They run through native ONNX Runtime CPU
inference. The desktop profile uses four inference threads and compact uses
two. The app does not need a Python environment or local web service.
[BGE model documentation](https://huggingface.co/BAAI/bge-base-en-v1.5),
[quantized base export](https://huggingface.co/Xenova/bge-base-en-v1.5/tree/main/onnx),
[quantized small export](https://huggingface.co/Xenova/bge-small-en-v1.5/tree/main/onnx).

The first nonblank semantic search downloads missing model assets with visible
progress. Search settings can download ahead of time, cancel, or download again.
Model URLs use pinned repository revisions and SHA-256 hashes. Downloads use
atomic files and are rejected on size/hash mismatch; inference verifies them
again before loading. Text matching remains usable after cancellation or a
download error.

Search settings provide **Test model** for the selected embedding provider. A
local test runs three fixed, non-card sentences through the installed model and
checks that every vector is finite, normalized, and has a stable dimension. A
remote test sends only those same three sentences to the configured endpoint,
normalizes the returned vectors locally, and compares their cosine similarities;
it never reads cards, writes the search cache, or includes an API key in its
result. Errors retain a stable `SEARCH_TEST_*` code and an actionable message.
Missing local assets are reported as a failed test and must be downloaded
explicitly with **Download local model** first. An in-progress test can be canceled;
late worker or network responses are discarded by a generation token.

The current desktop and Android builds use ONNX Runtime's CPU execution
provider. The model settings do not claim GPU acceleration unless the linked
ONNX Runtime package is built with a platform provider; GPU provider selection
will be added with the platform-specific runtime packages rather than silently
falling back while reporting success.

Normal application storage uses the platform's writable data directory under
`BetterFlash/models`. `BETTERFLASH_MODEL_DIR` overrides it. On this development
machine, `run-desktop.sh` defaults to `/workspace/betterflash-models`, shared
across worktrees. Model assets are not committed to git or copied into card
backups.

The semantic document includes both card faces, tags and deck ancestry. Images
contribute their text labels, not pixels. URLs and image file paths are removed.
Long text is split into overlapping passages; a card's score is its best passage
similarity. Recognizable mathematical notation also gets deterministic concept
terms: multiplication, division, percentage calculations or equation solving,
for example. These terms affect semantic indexing only, not exact-match labels.
This helps bridge terse expressions such as `3x=21` and natural-language
queries. It does not solve a problem or guarantee that every related card will
be retrieved.

Card-pane semantic retrieval adds up to twelve candidates with cosine scores at
least 0.35, after excluding cards already present in a text layer. Deck-content
search retains all candidates above that threshold so one deck cannot crowd
other matching decks out of the results. The
threshold is an initial retrieval heuristic, not a probability or accuracy
claim, and needs refinement with actual deck use. Spelling tolerance comes
primarily from the fuzzy layer; embeddings add meaning-based recall.

## Cache and responsiveness

Token matching, inference and cosine ranking run on a dedicated worker. Its
SQLite cache lives at `<collection directory>/search/embeddings.sqlite`, with
its own connection and schema. It never modifies the collection's card or
scheduling tables. No vector database or approximate-nearest-neighbor server
is required for a selected-deck or library-wide search.

Card search and deck-content search keep independent query state, cancellation,
results and errors. They share one worker thread, embedding model and vector
cache. Local inference yields between passages so one search can progress while
the other is indexing. Changing shared semantic settings restarts both channels.

Cache identity includes model/provider configuration, query/document role and
the encoded text. Changed content, tags or deck names therefore produces new
vectors; different models never share vector spaces. The cache contains vector
data and hashes, not plaintext queries, card contents or API keys. Entries are
evicted in insertion order above 8,192 vectors or 64 MiB of vector payload;
checks occur at open and every 128 writes, so short bounded overshoot and
SQLite/WAL overhead are possible. Settings > Search > Rebuild search index
clears embeddings; the next query encodes the selected deck, or the whole
library when no deck is selected, again.

This iteration uses the existing in-memory card snapshot and exhaustive cosine
ranking within the selected deck or, when no deck is selected, across the
library. Very large collections still need profiling and a paged card model;
no large-collection performance claim is made here.

## Optional provider embeddings

Choose Remote provider and enter a compatible embeddings endpoint and model in
Search settings. The protocol sends `model`, string-array `input`, and
`encoding_format: float`; it expects indexed numeric vectors in `data`.
Requests are limited to eight passages per batch with timeouts and response
limits. Redirects are rejected for credential-bearing embedding calls.

Provider mode sends the query and uncached passages for all cards in the
selected deck, or all cards in the library when no deck is selected, including
both faces, tags, deck names and derived math terms.
**Search within decks** searches the entire library regardless of the selected
deck. In provider mode, it may send uncached passages from every deck.
The connection dialog explains this before saving. Provider charges may apply.
Cached vectors avoid repeatedly transmitting unchanged text.

A separate key uses the existing desktop Secret Service integration. After
saving the remote connection, the API-key field's reuse menu can select an existing
model API key for the same scheme, host, and effective port. The selected key
is resolved independently of the source page's Local or Remote model choice.
See [API-key reuse](api-keys.md). A chat model identifier is not an embedding model.
HTTP is accepted only on loopback, where a local compatible endpoint may omit
authentication. No API key is exposed as a readable QML property or stored in
the collection/cache.

Examples documented by providers include Mistral's `mistral-embed` at
`https://api.mistral.ai/v1/embeddings` and Google's OpenAI-compatible embedding
endpoint. These are configuration options, not live-provider verification.
[Mistral API](https://docs.mistral.ai/api/endpoint/embeddings),
[Gemini compatibility API](https://ai.google.dev/gemini-api/docs/openai#embeddings).
Groq's current public API reference does not list an embeddings endpoint, so
its chat/STT connection is not assumed to provide embeddings.
[Groq API reference](https://console.groq.com/docs/api-reference).

## Verification status

Desktop code was compiled and the native app launched for manual review. Model
downloads were checked against pinned sizes and SHA-256 hashes. Automated tests
remain paused at the user's request. Live provider requests and an Android APK
have not been verified. Focused behavioral coverage for ranking, cancellation,
cache changes and provider errors should follow when testing resumes.
