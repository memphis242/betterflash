# Ask AI about a card

**Ask AI** is available in a saved card's actions menu, preview, and detail
view. It opens the card detail modal with a chat pane on the right. The divider
can be dragged, or focused and adjusted with the left and right arrow keys.
Its width is remembered. Narrow windows use **Card** and **Ask AI** tabs so
neither view is squeezed into an unusable column.

Opening the pane does not contact a model. Write a question and choose
**Send**, or press Ctrl+Enter. Enter inserts a newline. The configured language
model receives the question, conversation history, and a snapshot of the
card's text, kind, deck name, and tags. Attached images are not inspected.
LLMs can be wrong, have knowledge cutoffs, and miss recent changes.

Use **New chat** to start a separate conversation about the same card. The
chat selector returns to previous conversations. Closing the pane or card
cancels an ongoing reply and retains the conversation. Failed or canceled
questions can be retried or discarded without deleting earlier messages.
Deleting a chat requires an explicit confirmation.

Each conversation retains its original card context. When the saved card's
front, back, or kind changes, that conversation is marked out of date. Start
a new chat to discuss the current content. Changing a tag does not invalidate
an existing conversation. Deck names and tags are refreshed for each new
request.

## Information retrieval only

The model can search saved cards and read matching card text. Retrieved cards
are identified in the response's source list. Collection retrieval is bounded
and excludes unreadable saved records; it does not alter the Library's search
or selection. Searches inspect the full text, and passage queries can focus
retrieved excerpts beyond the beginning of long cards. Only relevant snippets are sent to the configured model.

When **Settings > Web search** is enabled, the model can also request Tavily
searches using the configured key. Web search is off by default. Each submitted
question permits at most three web searches, six retrieval calls, and seven
model turns. Tavily searches use the user's quota and may incur charges.
Search results are not a guarantee of accuracy. Source links open only when
clicked. The chat does not fetch arbitrary URLs or local files.

The tool dispatcher accepts only the named retrieval operations. It exposes
no tools for changing cards, decks, tags, schedules, files, or application
settings. Model instructions cannot grant additional capabilities. Card text,
conversation content, and retrieved material are treated as untrusted data.

## Local history and limits

Chats are stored in private, atomic JSON files under `card-chats` in the
collection's data directory. They are local to this installation and are not
included in collection sync or exports. Application credentials are not
injected into conversations or stored in their history.

Storage is bounded to 50 conversations per card, 200 messages per conversation,
and 2 MiB per card's history file. Questions are limited to 4,000 characters.
Model requests also have a context budget. Older messages may be omitted from
a request when needed; the pane reports that limitation while preserving the
visible history. A card and question that cannot fit are rejected with
configuration guidance.

If an assistant reply cannot be saved, it remains in memory with **Retry
saving**. Questions that could not be saved stay in the composer. Discarding unsaved changes requires an explicit choice. Corrupt or
unreadable history files are preserved and reported instead of overwritten.
A per-card lock and snapshot check prevent concurrent app instances from
silently overwriting another instance's saved conversations. On a conflict,
use **Reload saved chats** before retrying the action. An **Unsaved chat**
button in the top bar reopens recovery controls, even if the card has since
been deleted.

Automated tests are paused during visual refinement. Compilation and review
do not establish the quality of live model replies or Tavily results; those
require manual verification with a configured model.
