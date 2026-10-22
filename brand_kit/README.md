# BAMF - The Operator brand kit

Version 1.0 | October 22, 2026

BASH Application Management Framework, an Automate the Cloud project.

## Start here

Open **brand-preview.html** for an offline visual inventory, or **BAMF-Brand-Guide.pdf** for the short usage guide.

The approved design is the green-teal Operator gorilla with the small Automate the Cloud (ATC) cloud badge on its left shoulder. `source/approved-concept.png` is the approved concept, kept as a record. The production vectors redraw its shading in the official flat colors, and place the cloud badge from the original ATC SVG (in `reference/`) instead of redrawing it, at the smaller approved size.

## Contents

| Folder | Contents |
|---|---|
| logos/svg | 13 vector logos, all paths: stacked, compact, horizontal, wordmark, mascot, mono, and background versions |
| logos/png | Each logo exported at 512, 1024, and 2400 pixels wide; transparent where appropriate |
| icons | Mascot avatar SVG/PNGs; B favicon SVG, ICO, PNGs; web manifest and HTML snippet |
| social | 1200 x 630 GitHub social card, SVG and PNG |
| fonts | Official font sources, font-fetch script, self-hosting CSS, type scale; font binaries not bundled |
| tokens | Full light/dark palette in CSS, JSON, JavaScript, and GIMP palette formats |
| docs | README example and practical usage rules |
| source | The approved concept image, kept as a record |
| reference | Original supplied ATC SVGs, unchanged |

## Choose the right logo

- **bamf-stacked-light.svg**: default on white or the light surface.
- **bamf-stacked-dark.svg**: transparent asset for dark surfaces; pale lettering and a narrow external mascot keyline.
- **bamf-stacked-on-light.svg / on-dark.svg**: background included, useful when the host surface is unknown.
- **bamf-compact-light.svg / dark.svg**: mascot plus BAMF; no small subtitle.
- **bamf-horizontal-light.svg / dark.svg**: wide banners or headers.
- **bamf-wordmark-light.svg / dark.svg**: just BAMF, for short header spaces.
- **bamf-mascot.svg**: standalone character, intended for light surfaces.
- **bamf-mono-ink.svg / white.svg**: one-color production, with transparent negative spaces.
- **icons/avatar.svg**: square profile asset with a light background.
- **icons/favicon.svg**: a simplified B extracted from the wordmark, readable at small sizes. This is a new BAMF micro mark, not the ATC cloud favicon.

SVG logos contain paths, not embedded PNGs or live text. They scale without needing the fonts. Small contour changes from tracing the approved raster are expected. PNG exports at 2400px are rendered from vector paths, not enlarged raster previews. Do not use the source concept as the production master.

## Font setup

The font files are not included. Run `python3 fonts/fetch-fonts.py` on an Internet-connected machine, or use the official links in `fonts/README.md`. The script downloads nine TTF files and three OFL license files. Keep those licenses with any redistributed fonts. The BAMF custom lettering itself is artwork; installing these fonts will not reproduce it.

## Colors

- Brand: **#1D9E75**
- Light ink / mascot: **#0E1A18**
- Light surface / mascot highlights: **#F9FBFA**
- Dark surface: **#0B1513**
- Dark primary text: **#E6EEEC**

Use role tokens for interfaces. Brand teal is an accent, not the light-mode color for small text or links; use action **#0B6B52** instead. Full role mappings are in `tokens/colors.json`.

## Production notes

Use the supplied SVG for large or print work. Review a physical proof for small merchandise: the cloud dither is detailed. The files use RGB/sRGB; request a printer-specific conversion for process printing. Do not treat a color conversion as a new identity palette.

## Rights

The BAMF name, logos, mascot and icons, and the Automate the Cloud marks in `reference/`, are the property of Automate the Cloud Inc. They are not covered by the Apache License 2.0 that applies to BAMF's code; see [NOTICE.md](NOTICE.md). Fonts keep their own licenses.
