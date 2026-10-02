# fontlab — compare UI fonts the way the T-Deck Pro panel shows them

DeckPoint picked its compact UI font (Ubuntu, mono-hinted, pixel-sized) by
rendering candidates exactly as the 240x320 1-bit panel would and judging them
on the glass. These scripts reproduce that comparison.

- `specimen.py BODY SMALL` — one sheet: every candidate x three rasterizations
  (A: today's threshold >= 2/16, B: >= 8/16, C: FreeType mono hinting).
- `fontpages.py` — one full-screen 240x320 BMP per candidate (mono hinting).
  Copy them to the SD card (USB Drive mode) and open them in the File Browser.

Setup (fonts are OFL, from github.com/google/fonts):

    python3 -m venv .venv && .venv/bin/pip install freetype-py fonttools pillow
    mkdir fonts   # AtkinsonHyperlegible, Inter, FiraSans, SourceSans3, IBMPlexSans .ttf
    .venv/bin/python specimen.py 15 12 && .venv/bin/python fontpages.py

Shortlist kept for later: Fira Sans, Atkinson Hyperlegible, IBM Plex Sans and
Inter looked strong but lose detail at 1 bit; re-run once the panel driver
does grayscale (plan Phase 3b) — with anti-aliasing they may overtake Ubuntu.
Variable fonts (Inter, Source Sans 3, Plex) render their default instance;
pin a weight before judging them.
