# DeckPoint — next steps

Read `PROGRESS.md` first (dev loop, conventions). Full roadmap with rationale:
`/home/jtd/.claude/plans/abstract-weaving-pike.md`. Last updated 2026-10-02.

## 1. In progress — reader layout bug: TOC entries shifted off the left edge
- Repro: Red Rising 3-Book Bundle (`~/Downloads/Red Rising 3-Book Bundle*.epub`) → reader menu →
  Select Chapter → "Contents" under *Red Rising* → page 2/8. Screenshot shows "rologue" and
  "Helldiver" at x≈0; the markup is "Prologue" / "1: Helldiver" — every `div.toc_chap` line is
  ~45 px too far left (expected x≈34). `div.toc_part0` ("Part I: Slave") renders fine.
- Markup: `OEBPS/xhtml/01_Brow_9780345539793_epub_toc_r1.xhtml`; CSS
  `OEBPS/css/9780593725320_style1.css`: `div.toc_chap {margin-left:1.6em; text-align:left;
  text-indent:0; font-size:0.9em; line-height:1.4em}`; `toc_part0` has no text-indent and 1em.
  Content is `<div class="toc_chap"><a class="hlink">…</a></div>` (inline anchor directly in a div).
  CSS is innocent; it is a layout-engine bug.
- Already ruled out: `ParsedText::resolveFirstLineIndent` (explicit 0 can't go negative); the LTR
  word placement in `ParsedText::extractLine` starts at indent/alignment offset and only moves right.
  Next suspects: how block `marginLeft` (em at 0.9em font) and the page margin are added at render
  (`lib/Epub/Epub/blocks/TextBlock.cpp`, `Page.cpp`), the explicit `text-indent:0` vs undefined path,
  `extraStartOffset`, and the div→anchor inline style merge (`BlockStyle.h` ~L89).
- Plan: reproduce with the host tests (`test/` CMake suite, e.g. `test/chapter_html_slim_parser`)
  using a minimal snippet of that markup + CSS, find the negative offset, fix, add a regression test.
  (Was about to hand this to an `ed3d-basic-agents:opus-general-purpose` agent.)
- Upstream-worthy if it reproduces on stock CrossPoint.

## 2. Remaining UI pass (item "2" — mostly done)
- Key legend overlaps full-screen images in the BMP viewer (draw it only where there's room, or
  reserve the band / hide it in image views).
- Cap em-based CSS side margins on narrow screens (book CSS like `div.part {margin:0 2em}` leaves
  a ~180 px rivered column) — user hasn't confirmed; ask before doing.
- File Browser: long names wrap to 2 lines (consider single-line truncation); File Transfer list
  subtitles; any other screens not yet visited (reader sub-menus: Text Settings, Bookmarks,
  Look Up, Go to %).
- `KeyboardEntryActivity` legend strings are English literals (fine for now; i18n later).
- Remove `-DDECKPOINT_KEY_DEBUG` from release builds eventually.

## 3. Grayscale follow-ups
- Gray test card: light band looked closer to mid-gray than light; consider separate light/dark
  tuning and a third frame value; verify on more images (covers, the Shadow Order cover).
- Text vs image tuning currently shared (2/1 works for both — keep unless photos say otherwise).
- Sleep screen / cover images now get gray too — check them on glass.
- Re-run the font contest with AA (Fira Sans, Atkinson Hyperlegible, IBM Plex Sans, Inter kept as
  contenders; `tools/fontlab`; pin weights for variable fonts).

## 4. Keyboard-native reader (plan Phase 4 — user wants to feel 2 & 3 first)
- Vim keys (j/k/space/b, counts, gg/G, ]]/[[, `/` search with n/N, marks), command palette home
  (`:` commands, `#` chapters, `%` go-to over `LibraryIndex`), hint-mode dictionary (`d` labels),
  highlights & notes (port CrossInk's clippings stack + typed note field), CrossInk quick actions /
  stats / dictionary extras (see plan Phase 4b).

## 5. Hardware / power
- **Wake on keypress**: OR `BoardTDeckPro::keyboardWakeMask()` (GPIO15, RTC-capable) into the ext1
  deep-sleep wake mask and call `BoardTDeckPro::prepareForSleep()` before sleeping (not wired yet).
- Identify the side "volume up" button wiring.
- Touch (v1.1 CST3530 @0x1A, IRQ-driven; SDK lacks this controller) — user flagged as coming.
  Revisit `hasTouch()`-gated layouts (`hidesButtonHints()` already distinguishes keyboard vs touch).
- Haptics (DRV2605 on v1.1), light sensor, gyro tilt, LoRa quote sharing (plan Phase 5).

## 6. Temperature-compensated gray (plan Phase 6)
- Read BQ27220 battery temperature before each gray pass; scale light/dark frames from a small
  table; calibrate with gray-card photos at ~2 extra temperatures. (UC8253's own sensor needs a
  read-capable bus.) No CrossPoint/CrossInk/SDK code compensates custom LUTs today.

## 7. Docs & credits (user requirement)
- README section crediting **CrossPoint Reader** and **CrossInk** as inspirations (not just code
  sources), plus FreeInk SDK, Meshtastic, LilyGo; `CREDITS.md` mapping pieces to origins.
