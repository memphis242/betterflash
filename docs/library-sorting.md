# Library card sorting

The downward sort arrow beside the card filter opens the card sorting list.
Add **Creation date** or **Last modified date** to the list. The first selected
field is the primary key; each later field breaks ties in all preceding fields.
The numbered rows show that priority. Move a row up or down to change priority,
or remove it to stop using that field. Each date can use newest-first or
oldest-first order. Equal keys retain the existing card order.

Sort choices persist across app launches and apply to the cards currently shown
by the deck scope, search, and filters. Search relevance groups stay together;
the chosen keys sort cards inside each group. Pagination uses the sorted list,
and changing sort keys returns to the first page without clearing card selection.

Selecting date sort keys turns off **Weakest first**. Turning **Weakest first**
on clears date sort keys. **Reset sorting** removes the selected date keys and
restores the default card order, including the normal search relevance order.

Unknown dates sort last in either direction. Card date persistence and the
limitations for existing collections are described in [card dates](card-dates.md).
The previous/next page arrows are disabled and faded at their respective page
boundaries, including when no page is available.
