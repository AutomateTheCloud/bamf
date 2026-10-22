# BAMF typography collection

BAMF uses Automate the Cloud's supporting typefaces. The BAMF wordmark is custom artwork, stored as vector paths; no installable font exactly reproduces that lettering.

| Family | Weights | Role |
|---|---|---|
| IBM Plex Mono | 400, 500, 600, 700; 400 italic | Headings, code, labels, tables |
| Space Grotesk | Variable 300-700, normal | Body copy and interface text |
| IBM Plex Serif | 400, 600; 400 italic | Occasional pull quotes and epigraphs |

## Get the font files

**The font files are not included.** This folder holds the setup files and a script that downloads the original fonts.

From this directory, run:

```sh
python3 fetch-fonts.py
```

The script fetches nine original TTF files plus the three corresponding SIL Open Font License files from the official Google Fonts source repository. Download failures are reported; a nonzero exit means the collection is incomplete. Keep each OFL.txt alongside redistributed fonts. Font licenses do not cover the BAMF or Automate the Cloud logos.

For desktop use, install the downloaded .ttf files using your operating system's font manager. For websites, load fonts.css after fetching the files. TTF web loading works directly; WOFF2 conversion is optional and not supplied.

Official manual download sources:

- IBM Plex: https://github.com/IBM/plex/releases
- IBM Plex Mono: https://fonts.google.com/specimen/IBM+Plex+Mono
- IBM Plex Serif: https://fonts.google.com/specimen/IBM+Plex+Serif
- Space Grotesk: https://github.com/floriankarsten/space-grotesk/releases
- Space Grotesk: https://fonts.google.com/specimen/Space+Grotesk

## Suggested type scale

| Role | Face | Weight | Size / leading |
|---|---|---|---|
| Display | IBM Plex Mono | 600 | 40px / 1.15 |
| Section | IBM Plex Mono | 600 | 26px / 1.15 |
| Subheading | IBM Plex Mono | 600 | 19px / 1.2 |
| Body | Space Grotesk | 400 | 16px / 1.6 |
| Small | Space Grotesk | 400 | 13.5px / 1.5 |
| Label | IBM Plex Mono | 500 | 12px / 1.4, tracking .06em |
| Code | IBM Plex Mono | 400 | 13.5px / 1.6 |
| Quote | IBM Plex Serif | 400 | 19px / 1.4 |
