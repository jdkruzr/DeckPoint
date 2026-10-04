# Credits

DeckPoint is a small project standing on large shoulders. This file is the thank-you note;
license texts live in the files named below.

## Acknowledgements / Inspirations

### CrossPoint Reader

DeckPoint is a fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader).
Almost everything you see when you read a book on it (the EPUB engine, layout cache, library,
themes, file transfer, OPDS, KOSync, and the whole "a reader should be small, fast and yours"
attitude) is the CrossPoint community's work. Our base upstream commit is `f331030f`. Without
that vision there would be nothing to put a keyboard on. Thank you.

### CrossInk

**CrossInk**, by **uxjulia**, is the other half of the inspiration. CrossInk showed what a
CrossPoint-based reader looks like when it grows opinions: a highlights-and-clippings workflow,
quick actions, reading stats, and CrossInk-style swipe and tap controls. DeckPoint's
highlights, notes and export, its touch work, and its general "make the reader feel personal"
direction are shaped by what CrossInk got right. If you read on an Xteink device and want that
flavor, go look at CrossInk.

How much of that is ported code versus plain inspiration is spelled out honestly in the table
below. Short version: the repo records CrossInk-originated features arriving through upstream
CrossPoint, and DeckPoint's own highlights stack is written from scratch with CrossInk as the
inspiration.

CrossInk lives at <https://github.com/uxjulia/CrossInk>.

## Also with thanks

- **[FreeInk SDK](https://github.com/Free-Ink/freeink-sdk)**: the hardware layer under
  CrossPoint (display, input, storage, UI toolkit). Vendored in `freeink-sdk/`; DeckPoint adds
  a T-Deck Pro board, panel driver and keyboard driver there. FreeInk in turn derives from the
  OpenX4 E-Paper Community SDK, credited in `freeink-sdk/NOTICE` (e-paper driver authorship to
  CidVonHighwind).
- **[KOReader](https://github.com/koreader/koreader)** and
  **[AnnotationSync.koplugin](https://github.com/dani84bs/AnnotationSync.koplugin)** (Daniele
  Trainini, MIT): DeckPoint reads and writes KOReader-compatible
  XPointers and AnnotationSync's file format so highlights round-trip between devices. This is
  format compatibility only; no KOReader or plugin code is included.
- **Meshtastic**: used as a hardware *reference* for how the T-Deck Pro's pins and panel
  behave. It is GPL-3, so **no code was copied**.
- **LilyGo**, for making the T-Deck Pro and publishing the schematics and example material that
  made bring-up possible. Their repository and `bb_epaper` are GPL-3 and were reference only.
- **GxEPD2**: the UC8253 driver follows its forced-temperature trick for the panel's OTP
  waveforms (see `Uc8253Gdeq031Driver.cpp`). TODO(user): confirm whether to list GxEPD2 formally
  and with which license.

## What came from where

Verified from the repository (git history, file headers, `// DECKPOINT:` markers,
`docs/deckpoint/PROGRESS.md`). Anything the repo cannot prove is marked TODO.

| Piece | Origin |
| --- | --- |
| Reader engine, library, themes, web server, OPDS, KOSync, i18n, most of `src/` and `lib/` | CrossPoint Reader (MIT) |
| CrossInk-originated changes that reached CrossPoint upstream: hr tag rendering, "read book move", File Transfer guard, CrossInk-style swipe and tap controls | Arrived via CrossPoint commits `7accc607`, `06d28d6f`, `cb995809`, `b6cbb5e1` |
| `freeink-sdk/` (display, input, storage, UI toolkit) | FreeInk SDK (MIT), vendored at base `bbd528c`; see `freeink-sdk/VENDORED.md` |
| T-Deck Pro board, UC8253/GDEQ031 panel driver, TCA8418 keyboard driver (`KeyMatrix`, `BoardTDeckPro`), `HalKeyboard` | DeckPoint, written from datasheets and schematics |
| Keyboard layer, `?` help, key legend, `:` command line, vim keys, hint-mode lookup, compact UI, font pipeline (`src/deckpoint/`) | DeckPoint |
| Highlights, note editor, `:notes`, Markdown / My Clippings export | Written from scratch for DeckPoint; the idea is inspired by CrossInk's clippings feature |
| KOReader XPointers, annotation store, AnnotationSync-format WebDAV sync | DeckPoint, written against KOReader's and AnnotationSync's observable formats |
| CrossInk quick actions, reading stats | Not ported: no such code in the repo (listed as ideas in `docs/deckpoint/NEXT_STEPS.md`). TODO(user): update if that changes |

## Licenses

| Component | License | Where |
| --- | --- | --- |
| DeckPoint, CrossPoint Reader | MIT, "Copyright (c) 2025 CrossPoint Reader organization" | `LICENSE` |
| FreeInk SDK (vendored) | MIT, "Copyright (c) 2026 FreeInk" | `freeink-sdk/LICENSE`, `freeink-sdk/NOTICE` (also carries the OpenX4 E-Paper Community SDK MIT notice) |
| CrossInk | MIT (inherits CrossPoint's license) | <https://github.com/uxjulia/CrossInk/blob/HEAD/LICENSE> |
| Meshtastic, LilyGo T-Deck-Pro repo, bb_epaper | GPL-3: reference only, no code copied | nothing included |
| Bundled libraries in `lib/` (expat, miniz, uzlib, ...) | their own licenses | each library's directory |

TODO(user): add your own copyright line for DeckPoint changes next to the CrossPoint one in
`LICENSE`, if you want one.
