# Model response cache

BetterFlash reuses accepted language-model responses for the same effective
request and model configuration. The cache is enabled by default. Each model
action has a **Fresh response** control: select it before starting the
action to bypass cache reads for the entire operation. A successful fresh
response replaces the matching cached response for later requests. The action
indicates when it used at least one cached model response.

The identity covers the complete model input, formatting and sampling options,
tool definitions, provider and API, endpoint, model, account, and applicable
workspace. Local responses also depend on the actual model file's SHA-256 and
file identity. The first cacheable use of an unchanged local model verifies its
file off the UI thread; later uses reuse that verification while the file's
identity remains unchanged. A changed file is verified again.

Caching applies to model turns, not to a complete multi-step action. An action
that uses retrieval may repeat its read-only tools even if the model's tool
request was cached. Web searches can therefore still use Tavily credits. A new
retrieval result changes the model input and naturally requires a fresh model
response. Unknown or write-capable tools are never made cacheable, and tool
validation and source checks still run. Provider connection tests and explicit
speech-model evaluations always use live responses and do not populate the
model-response cache.

Entries are private files under the collection data directory's
`cache/model-responses` directory. An isolated `--data-dir` has its own cache.
The cache is not included in collection exports, backups, checkpoints, or sync.
Entries hold model responses, which can include private card content and native
provider continuation blocks. Credentials are never intentionally written;
responses containing recognized credentials are excluded. Inspect files before
sharing them. The cache keeps at most 512 entries totaling 128 MiB, with a
1 MiB per-entry bound. A response is not reused after 30 days, or after 30 days
without access. Least-recently-used and inactive entries are removed when the
cache is accessed or updated.

The model provider can update weights behind an unchanged remote model name.
That change cannot be detected without making another provider request; choose
**Fresh response** when new information or a changed model is important.
Local and remote output can also be nondeterministic. A cache hit deliberately
reuses an earlier accepted output rather than generating a new variation.

Only provider-successful, complete responses accepted by their caller are
stored. Provider-signaled refusals and common refusal-like English prefixes are
not cached; no lightweight text heuristic can recognize every possible refusal.
Malformed entries and checksum mismatches are discarded and reported. If cache
I/O fails, an actionable warning is shown and the model can still run. The global
**Cache warning** action keeps notices accessible after an operation dialog closes;
open it for the cause and resource path, or dismiss it after reading. Cache
lookups do not replace the normal card, source, tool, or output validation.

The project currently pauses automated tests. Build and manual inspection do
not verify cache expiry, concurrency, filesystem failures, provider APIs, or
all consumer validation paths; these remain validation limits until exercised.
