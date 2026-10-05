# Third-party components

BetterFlash uses the system Qt installation for its native interface, SQLite
driver, networking, audio, and speech synthesis. Qt license notices are supplied
by that installation. Android packaging must retain the notices provided by its
Qt kit and OpenSSL libraries.

The build fetches two pinned source archives and verifies their SHA-256 checksums:

| Component | Version | License and source |
| --- | --- | --- |
| MicroTeX | `0e3707f6dafebb121d98b53c64364d16fefe481d` | [MIT](packaging/licenses/MicroTeX-MIT.txt), [source](https://github.com/NanoMichael/MicroTeX/tree/0e3707f6dafebb121d98b53c64364d16fefe481d) |
| tinyxml2 | `11.0.0` | [zlib](packaging/licenses/tinyxml2-zlib.txt), [source](https://github.com/leethomason/tinyxml2/tree/11.0.0) |

IBM Plex Mono regular, bold, italic, and bold italic are embedded without
modification for the interface and Markdown renderer. These font files are
distributed under the [SIL Open Font License](packaging/licenses/IBM-Plex-Mono-OFL.txt)
and were obtained from the [Google Fonts distribution](https://github.com/google/fonts/tree/0b58fb370093f9a9f4ff785d94405710b79de67c/ofl/ibmplexmono).

MicroTeX's Qt text painter is adapted at build time to use glyph outlines. Its
formula fonts and resource files are bundled without changing the fonts. Their
upstream notices include the SIL Open Font License, Knuth's font notice, the
dsrom notice, and the Greek and Cyrillic font licenses. Copies are in
`packaging/licenses/` and in the application resources.

The optional local recognition helper uses Vosk only when explicitly selected.
No Vosk library or model is bundled. Its setup script records the source and
license information for downloaded test assets. Remote providers retain their
own service terms; no provider credentials are included in this repository.
