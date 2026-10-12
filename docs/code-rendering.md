# Native Markdown code rendering

Fenced Markdown uses one text frame for each block, with a continuous background,
inner padding and no rules or gaps between source lines. Consecutive fences remain
separate, even when they name the same language. Backtick and tilde fences are
supported, including empty and unclosed fences. Inline code has a contrasting
background and heavier type. Code is excluded from the formula pass.

A language identifier after the opening fence selects a KDE Syntax Highlighting
language definition. Common aliases include `cpp`, `c++`, `cc`, `py`, `js`, `ts`,
`rs`, `sh` and `yml`. Unknown or omitted identifiers remain readable plain code.
The native engine carries lexer state between lines, including multiline strings
and comments. Fences nested within complex Markdown list or quote structures
retain Qt's native Markdown parsing and plain code presentation.

## Themes

Settings > Code rendering remembers independent dark and light theme choices.
The choices are derived from the actual available theme repository. Defaults are
Iceberg Dark and Iceberg Light. Explicit user selections remain remembered.
The repository includes KDE's shipped themes and
BetterFlash conversions of Rose Pine, Rose Pine Dawn, Everforest Dark/Light,
Jellybeans and Iceberg Dark/Light palettes. Jellybeans is a dark theme only.
Token mappings in the conversions are specific to this renderer. Text colors
are adjusted toward white or black when needed to reach 4.5:1 contrast against
the selected code background.

The palettes come from their upstream projects:

- [Rose Pine](https://github.com/rose-pine/palette)
- [Everforest](https://github.com/sainnhe/everforest)
- [Jellybeans](https://github.com/nanotech/jellybeans.vim)
- [Iceberg](https://github.com/cocopon/iceberg.vim)

## Language server

On desktop, visible C and C++ fences additionally request semantic tokens from
`clangd`, discovered on `PATH` by default. Settings can disable this feature or
select another clangd executable. No server is bundled or silently installed.
A missing or failing server leaves the native lexer colors in place and reports
an actionable status in Code rendering settings. Other languages currently use
the native syntax engine; Android does not start external language servers.

The client sends Language Server Protocol `initialize`, `initialized`,
`textDocument/didOpen`, `textDocument/semanticTokens/full`, and
`textDocument/didClose` messages over a local process's standard streams. Server
capabilities negotiate UTF-16 positions. Full token arrays and source bounds are
validated before overlaying theme colors. C23 and C++23 parsing commands are
supplied through clangd's `compilationDatabaseChanges` extension. These commands
are parsing descriptions, not subprocess launches or build actions.

The process uses a unique temporary workspace, with project configuration,
background indexing and clang-tidy disabled. No shell is used. Card content does
not control process arguments, workspace paths or executable selection. Snippets
are sent as document text and are not saved as source files. Standard headers may
be read by clangd while it parses a snippet; the workspace is not an operating
system sandbox. BetterFlash never requests compilation or execution of snippets.

The session handles one semantic request at a time, with two clangd worker
threads, at most 24 queued snippets, a 32,768 UTF-16-unit snippet limit, a 2 MiB
result cache, a 2 MiB protocol buffer limit and a 15-second request timeout.
The server stops after one idle minute and on application shutdown. Each visible view owns its requested source keys. Changed, hidden or destroyed
views release obsolete jobs, including cancellation of an active request. Valid
queued jobs retain FIFO ordering. A full queue defers further requests, and
capacity notifications resume them without dropping visible snippets. Results
are keyed by language and source, so they cannot recolor changed text.
Native highlighting is available immediately; partial snippets or missing
includes may yield incomplete semantic classification. A full renderer input is
bounded to 256 KiB of UTF-16 text. Evicted semantic entries are requested again
when a document is reopened, rather than starting a repaint/request loop.

## Build and dependencies

The build fetches official KDE Syntax Highlighting 6.18.0 and Extra CMake Modules
6.18.0 archives, verified by SHA-256. Only the native highlighting library is
linked into BetterFlash; KDE example applications, CLI tools, tests,
translations and documentation are not built. No browser engine is involved.
The highlighting dependency requires Qt 6.7 or later. KDE supplies the language definitions; their individual notices travel in the
resource data and aggregate licenses are under `packaging/licenses/`.

References:

- [KDE AbstractHighlighter](https://api.kde.org/ksyntaxhighlighting-abstracthighlighter.html)
- [Language Server Protocol semantic tokens](https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_semanticTokens)
- [clangd compilation-command extension](https://clangd.llvm.org/extensions#compilation-commands)

This change was compiled locally. Automated tests and GUI sweeps remain paused
at the user's request. Android rendering and packaging still require a build
with an Android Qt kit and device verification.

When cross-compiling, provide `-DKATEHIGHLIGHTINGINDEXER_EXECUTABLE=/absolute/path/to/desktop-build/bin/katehighlightingindexer`.
The Android build script supplies this from the same worktree's desktop build,
or from the `KATEHIGHLIGHTINGINDEXER_EXECUTABLE` environment override.
The desktop build creates this native build-time indexer from the same pinned
KDE version. Requiring that explicit host tool avoids accidentally trying to run
an Android executable during source generation.
