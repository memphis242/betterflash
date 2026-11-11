# Language model setup

The language model can run locally or through a remote endpoint. Select the
location in **Settings > Language model**. Local profiles download on demand;
see [local model support](local-models.md).

For a remote language model, select the API protocol, configure its endpoint,
choose or enter a model identifier, and save an API key. **Fetch models**
queries the configured endpoint only when requested. The editable model field
also accepts identifiers not shown in the catalog.

- **OpenAI-compatible** accepts providers using chat completions, including Groq.
  Enter the full chat completions URL for the provider.
- **Anthropic Messages** uses the native Claude Messages API. The default
  endpoint is `https://api.anthropic.com/v1/messages`. Use a Claude API key
  and fetch the models available to that account, or enter a current Claude
  model identifier manually. A workspace-scoped key needs no workspace ID;
  a key that can access multiple workspaces needs the appropriate ID.
  Anthropic models and availability can change; the model catalog reflects
  the configured account. Model listing is bounded to five pages and the UI
  reports when additional models may exist.

Switching API protocols remembers the configured endpoint and model for each.
Credentials use the existing provider-origin scoped key selection; secrets are
not stored in the collection or sent to another origin. Remote requests can
consume provider credits and may incur charges. **Test model** explicitly sends
a small request to validate the selection.

The native Anthropic pathway supports the model operations used by the app,
including retrieval tools in Ask AI. It uses API version `2026-10-21`, omits
removed sampling parameters, and retains signed thinking blocks within retrieval
exchanges. JSON-only operations request a JSON object through instructions and
validate the returned data before use. This is not schema-constrained output;
a malformed reply is reported as an error and is not applied to the collection. Native requests allow up to 120 seconds
and at least 8,192 output tokens, including reasoning; these are limits, not a
requirement to consume that many tokens. No thinking configuration is forced,
so the selected model uses its default behavior.

Provider replies remain subject to model
availability, billing limits, tool behavior, and incomplete or inaccurate
answers. See [Ask AI](card-chat.md) and [API-key reuse](api-keys.md).

API behavior was checked against the official [Messages reference](https://platform.claude.com/docs/en/api/messages/create),
[API version history](https://platform.claude.com/docs/en/api/versioning), and
[Models reference](https://platform.claude.com/docs/en/api/models/list).
