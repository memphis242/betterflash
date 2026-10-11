# Library card search

Search is scoped to cards directly in the selected deck. It does not silently
include descendant decks. Blank queries show that deck's ordinary card list.

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

Semantic retrieval considers up to twelve top candidates with cosine scores
at least 0.35. Duplicates already present in a text layer are omitted. The
threshold is an initial retrieval heuristic, not a probability or accuracy
claim, and needs refinement with actual deck use. Spelling tolerance comes
primarily from the fuzzy layer; embeddings add meaning-based recall.

## Cache and responsiveness

Token matching, inference and cosine ranking run on a dedicated worker. Its
SQLite cache lives at `<collection directory>/search/embeddings.sqlite`, with
its own connection and schema. It never modifies the collection's card or
scheduling tables. No vector database or approximate-nearest-neighbor server
is required for a selected-deck search.

Cache identity includes model/provider configuration, query/document role and
the encoded text. Changed content, tags or deck names therefore produces new
vectors; different models never share vector spaces. The cache contains vector
data and hashes, not plaintext queries, card contents or API keys. Entries are
evicted in insertion order above 8,192 vectors or 64 MiB of vector payload;
checks occur at open and every 128 writes, so short bounded overshoot and
SQLite/WAL overhead are possible. Settings > Search > Rebuild search index
clears embeddings; the next query encodes the selected deck again.

This iteration uses the existing in-memory card snapshot and exhaustive cosine
ranking within one deck. Very large collections still need profiling and a
paged card model; no large-collection performance claim is made here.

## Optional provider embeddings

Choose Remote provider and enter a compatible embeddings endpoint and model in
Search settings. The protocol sends `model`, string-array `input`, and
`encoding_format: float`; it expects indexed numeric vectors in `data`.
Requests are limited to eight passages per batch with timeouts and response
limits. Redirects are rejected for credential-bearing embedding calls.

Provider mode sends the query and uncached passages for all cards in the
selected deck, including both faces, tags, deck names and derived math terms.
The connection dialog explains this before saving. Provider charges may apply.
Cached vectors avoid repeatedly transmitting unchanged text.

A separate key uses the existing desktop Secret Service integration. Reusing
the language-model key is an explicit setting and requires matching scheme,
host and effective port. A chat model identifier is not an embedding model.
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
