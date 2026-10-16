# Reusing API keys

Each remote model configuration has one masked API-key field. Click it or
choose **Reuse saved key** above it to select a key entered on another model
settings page. Continue typing to enter a new key for the current feature.
Typing does not replace the active key until **Save key** is pressed.
Selecting an existing key changes the reference used by that connection;
it does not copy the secret into another keyring entry.

Key names come from the page where they were entered:

| Settings page | Key name |
| --- | --- |
| Voice > Speech recognition | `speech-recognition-api-key` |
| Voice > Speech output | `speech-output-api-key` |
| Language model | `language-model-api-key` |
| Search | `embedding-api-key` |

The picker also shows the provider address. Keys for different provider
addresses can have the same page-based name, but remain separate entries.
Only keys with the same scheme, host, and effective port as the connection
are offered for reuse. Changing an endpoint to another provider does not carry
the previous provider's key with it.

For example, save a Groq key in Speech recognition, then select
`speech-recognition-api-key` in the remote Speech output or Language model
settings configured for `https://api.groq.com`. Model availability and account
permissions still depend on the provider. A key alone does not enable an
endpoint the provider does not offer.

For remote Search, save the endpoint first, then select or enter its API key.
You can leave the model empty while configuring the key, fetch the model list,
and save the connection again after choosing a model. A compatible embedding
model is required for search; a chat model cannot
substitute for one. Groq's public API reference does not document an embedding
endpoint, so use local embeddings or a compatible embedding provider.
[Groq API reference](https://console.groq.com/docs/api-reference)

## Updating and removing keys

Edit or clear a shared key from its source settings page. Every connection
referencing that key sees the update. Switching a connection back to its own
key leaves the shared source key intact. Changing the source page's model
location to Local does not remove its saved remote key.

If the source page now uses a different provider, switch it to the provider
address shown beside the shared key and choose its own feature key. The
original key remains associated with that address.

Existing keys retain their current keyring entries and receive the automatic
page-based names. Independently entered keys remain separate even if their
secret values happen to match. No key is merged or deleted automatically.

## Storage

The desktop Secret Service keyring stores the secrets. Application settings
contain only source names, provider addresses, and selected key identifiers.
Key values stay out of collection data, backups, synchronization payloads,
logs, and readable QML properties.

Provider associations for older keyring entries are pinned in the keyring
itself. If that association cannot be saved, Settings explains how to restore
access; the older key is retained.

The status beside the controls distinguishes a key saved on this computer
from one available only for the current session or supplied through an
environment variable. Reusing a session-only key does not make it persistent.
If a selected key is removed or unavailable, the connection keeps its selected
reference and reports the missing key instead of substituting another one.

Synchronization access tokens remain separate from the model API-key picker.

## Available remote models

The button beside a remote model selector fetches the provider's model list.
Select a returned model or enter an identifier manually. Fetching the list
does not run inference or change the configured model. Opening Settings does
not fetch automatically.

Groq provides a combined list at `https://api.groq.com/openai/v1/models`.
BetterFlash groups the known Whisper and speech-output model families for the
speech selectors. Other or newly introduced identifiers remain enterable
manually. Compatible providers that do not describe model capabilities may
return a combined list; choose a model suitable for the feature.
[Groq model list](https://console.groq.com/docs/models)

Requests use the selected provider key and the same provider address as the
configured endpoint. Changing a provider or credential discards its fetched
list. Errors leave the selected model intact.
