# Transcript correction pipeline studies

Open the native comparison window with:

```sh
scripts/run-desktop.sh --transcript-flow-designs
```

1. **Glyph chain** - unboxed icons and short labels linked by arrows.
2. **Ledger lane** - one frame around the complete pipeline, with stage notes.
3. **Signal path** - unboxed stages above a shared track with hollow markers.
4. **Editorial stages** - icons beside longer, descriptive labels.
5. **Transcript strip** - one frame around the flow and an illustrative text correction.

The app uses **03 Signal path**. Enabling correction highlights the
language-model icon and directs the arrows through that stage. Disabling it
mutes the icon and routes recognition directly to the final text along a
curved bypass beneath the language-model stage. Stage positions stay fixed.

The preview correction switch also shows these two routes in Signal path.
The window does not access recordings, cards, provider credentials, or models.
Its example transcript is illustrative.
