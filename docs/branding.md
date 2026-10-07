# BetterFlash branding assets

The canonical Recall fold mark is an editable vector on a transparent 512 by
512 square artboard:

- `packaging/branding/recall-fold.svg` uses ivory `#f7eee8` for the warm dark
  theme.
- `packaging/branding/recall-fold-light.svg` uses dark aubergine `#322832` for
  the light theme.
- `packaging/betterflash.svg` is the launcher artwork: the ivory mark on a
  rounded aubergine `#211722` square.

The mark keeps the original tall bookmark proportions and includes its curled
upper-left return, upper triangular opening, diagonal fold band, lower panel,
and V-shaped tail.

- `packaging/branding/wordmark.svg` is the immutable outlined IBM Plex Mono
  wordmark source.
- `packaging/branding/betterflash-lockup.svg` and
  `betterflash-lockup-light.svg` are centered stacked mark and wordmark
  lockups derived from those two canonical vectors.

The wordmark is an outline of IBM Plex Mono and does not require a font at
runtime. IBM Plex Mono is distributed under the SIL Open Font License; the
corresponding notice is `packaging/licenses/IBM-Plex-Mono-OFL.txt`.

- `packaging/branding/png/dark/<size>.png` is the ivory mark.
- `packaging/branding/png/light/<size>.png` is the dark mark.
- `packaging/icons/<size>x<size>/betterflash.png` is the launcher icon.

Each family contains 16, 20, 22, 24, 32, 40, 48, 64, 96, 128, 192, 256,
512, and 1024 pixel exports. The Android vector at
`android/res/drawable/betterflash_icon.xml` uses the same path geometry.

Regenerate all derived vectors and committed PNGs after changing either
canonical SVG with:

```sh
./scripts/export-brand-icons.sh
```

The exporter needs Python 3 and ImageMagick. It runs the standard-library Python
vector generator first, then renders the PNGs with `magick`. Set `SVG_RENDERER`
to `convert` when using an older ImageMagick installation. The SVGs are rendered
above the largest export size and downsampled with transparent backgrounds;
PNG metadata is stripped for reproducible output.
