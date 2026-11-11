# Card correctness and completeness checks

The new card, edit card, generated draft, and Atomicize candidate editors offer
**Check correctness** and **Check completeness** before saving. Each action
checks a snapshot of the current unsaved front and back, using the configured
language model. A correctness check examines factual accuracy and internal
logic. A completeness check looks for missing answer components, necessary
qualifications, and misleading partial truths within the question's scope.

The result appears in a separate, bounded dialog with a summary, possible
findings, suggested adjustments, and any retrieved source links. Suggestions
are advisory. Return to the editor to make changes and run another check on
the updated draft. Saving or replacing cards remains an explicit action.
Closing the result dialog cancels an ongoing check. Editor shortcuts are
suspended while the result is open.

Every result is prefaced with a warning: LLMs can be wrong, may have a knowledge
cutoff, and may miss recent changes. Search results can also be incomplete or
misleading. The app reports whether the model actually searched, rather than
implying that allowing search means it consulted evidence. Checks inspect text;
attached images are not sent to the model for visual inspection.

Read-only card detail and saved-card previews expose both checks in the card's
**Card actions** (`...`) menu. The shared library card menu offers them too.
These actions check a snapshot of that card's saved content and keep the preview
open underneath the result. Returning from the check preserves the preview's
side and scroll position. Cards requiring repair cannot be checked from the
menu until they are readable.

## Optional web search

Both checks work using the model's intrinsic knowledge. **Settings > Web
search** separately enables Tavily search, off by default. Save an existing
Tavily API key there using the app's credential controls, then enable the
switch. The key is scoped to Tavily's HTTPS origin and stored through the
existing secret store; it is not placed in cards, sync data, or model prompts.

Enabling search allows the model to request up to three basic searches during
an explicitly started card check. Model-generated queries derived from the
card are sent to Tavily. Result snippets are sent to the configured language
model. Searches use the user's Tavily quota and may incur charges under their
account plan. There are no background searches or automatic retries. The
integration explicitly disables Tavily's automatic search-depth selection.

Remote chat-completion models use native `web_search` function calls, one at a
time. Local models use a structured JSON search-request protocol. Remote
models can also express that JSON action, but the remote endpoint must accept
the tools request when search is enabled. If a provider does not support the
required interface, the error offers model configuration guidance. A failed
search is reported explicitly. **Retry without web search** runs the same draft
through the model without changing the global search setting or leaving the
editor.

The loop permits at most four model turns. Network requests have time and size
limits, and evidence is bounded to the selected model's context budget. The
result indicates shortened evidence. Only source IDs actually supplied to the
model may be cited by structured findings. Source links come from Tavily, are
restricted to HTTP(S), and open only on an explicit click. The app does not
fetch arbitrary source URLs or execute model-requested tools other than search.
Card content and web results are labeled as untrusted data in model prompts.
These precautions constrain tool execution; they do not establish factual
truth or make a language model immune to misleading content.

## Integration choice and validation

Tavily is accessed directly through Qt networking and its REST Search API.
No CLI, Python runtime, browser engine, or MCP server is required by the app.
This follows Tavily's application-integration guidance. A self-hosted SearXNG
service is a possible future alternative, but is not implemented here.

References:

- [Tavily setup guidance](https://www.tavily.com/agent-setup/SKILL.md)
- [Tavily Search API](https://docs.tavily.com/documentation/api-reference/endpoint/search)
- [SearXNG API](https://docs.searxng.org/dev/search_api.html)

Automated tests are paused during visual refinement. Compilation and code
review do not verify live model or Tavily behavior. A real check with a
configured model and search key is still required for manual acceptance.
