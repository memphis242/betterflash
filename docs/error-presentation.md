# Error presentation and provider diagnostics

Errors use the shared `ErrorBlock`: a bordered error panel with a prominent
cause, a stable code, a separate recommendation, and collapsed technical details.
Explicit action fields take precedence; legacy messages with a recognizable
remedy sentence display that sentence separately. Provider response bodies must
never be concatenated into the brief message.

Model validation places failures before input/output examples. A failed API
request has no transcript or answer to score: empty model output and normalized
scoring are omitted. A returned transcript that differs from the expected phrase
still has its word-error and normalized scores.

HTTP diagnostics retain the status, network cause, and response body where
available. Bodies are bounded (normally 16000 or 16384 characters); truncation is
explicit. Credentials are redacted before details reach the UI or logs. An empty
body or a connection failure cannot provide an upstream error message. No
successful audio body is shown as error evidence.

Authentication failures recommend correcting credentials/access. Rate limits
recommend waiting; server failures recommend retrying or checking service status.
Disabling model tools is offered for invalid model requests, not authentication
failures. A failed Tavily request can still offer a deliberate knowledge-only
check, since that bypasses the failing search service rather than the model key.

## API surface audit

The error presentation pass covers speech recognition tests and runtime errors,
speech output, transcript correction, language model tests and summaries, model
catalog discovery, local model downloads, embedding tests and searches, Tavily
connection tests and tool calls, sync, card generation/refinement, atomicization,
correctness/completeness checks, Ask AI, study guidance, checkpoint summaries,
and tag generation. These share the presentation component; service-specific
controllers retain their own bounded evidence and operation lifetime guards.

Automated testing is paused by project policy. Compilation and source inspection
are the validation gate for this pass; provider-specific live failures and visual
behavior still require manual confirmation.
