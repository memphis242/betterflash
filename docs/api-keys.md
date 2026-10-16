# Reusing API keys

Each remote model configuration has a **Key source** picker. Keep that page's
own key selected to enter a new key, or choose an existing key from another
model settings page. Selecting a key changes the reference used by that
connection; it does not copy the secret into another keyring entry.

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

For remote Search, save the endpoint and model first, then select or enter
its API key. A compatible embedding model is required; a chat model cannot
substitute for one. Groq's public API reference does not document an embedding
endpoint, so use local embeddings or a compatible embedding provider.
[Groq API reference](https://console.groq.com/docs/api-reference)

## Updating and removing keys

Edit or clear a shared key from its source settings page. Every connection
referencing that key sees the update. Switching a connection back to its own
key leaves the shared source key intact. Changing the source page's model
location to Local does not remove its saved remote key.

Existing keys retain their current keyring entries and receive the automatic
page-based names. Independently entered keys remain separate even if their
secret values happen to match. No key is merged or deleted automatically.

## Storage

The desktop Secret Service keyring stores the secrets. Application settings
contain only source names, provider addresses, and selected key identifiers.
Key values stay out of collection data, backups, synchronization payloads,
logs, and readable QML properties.

The status beside the controls distinguishes a key saved on this computer
from one available only for the current session or supplied through an
environment variable. Reusing a session-only key does not make it persistent.
If a selected key is removed or unavailable, the connection keeps its selected
reference and reports the missing key instead of substituting another one.

Synchronization access tokens remain separate from the model API-key picker.
