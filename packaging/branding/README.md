# BetterFlash walnut marks

The colored sole walnut is the primary app identity. The blueberry and spinach
pairings are bundled for selective future use. Keep their original colors in both
themes; these are brand illustrations, not state indicators or action glyphs.

| Asset | Purpose | QML variant |
| --- | --- | --- |
| `walnut.png` | Primary header, window and launcher mark | `walnut` (default) |
| `walnut-blueberry.png` | Reserved alternate mark | `walnut-blueberry` |
| `walnut-spinach.png` | Reserved alternate mark | `walnut-spinach` |

All masters are square 1254px PNGs with alpha transparency and built-in padding.
The reusable `BrandMark` component preserves their proportions and decodes to the
display size at the screen's pixel ratio. For example:

```qml
BrandMark { variant: "walnut-blueberry" }
```

Original approved presentation boards remain under `docs/design/food-logos/`.
Transparent production assets were prepared with the built-in image generation
tool. Exact extraction prompts and reference paths are recorded in `prompts.json`.

Run `scripts/export-brand-icons.sh` with ImageMagick installed to reproduce the
committed desktop sizes (16, 24, 32, 48, 64, 128, 256, 512px) and Android density
icons (48, 72, 96, 144, 192px). ImageMagick is an asset maintenance dependency,
not a build or application dependency. Normal builds consume committed PNGs.

Android packaging is a draft pending a build with an Android Qt kit and device
verification. Its launcher uses the same sole walnut master.
