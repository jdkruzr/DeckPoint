# DeckPoint — next steps

Read `PROGRESS.md` first (dev loop, conventions). Full roadmap with rationale:
`/home/jtd/.claude/plans/abstract-weaving-pike.md`. Last updated 2026-10-02.

## 1. CSS stylesheet scoping (follow-up to the TOC bug, fixed in bef53e2d)
- `Epub::parseCssFiles` (`lib/Epub/Epub.cpp:240-340`) merges every manifest `.css` into one rule
  set; pages get rules from sheets they never `<link>`. Red Rising TOC: chapter lines now start at
  the margin (hanging-indent clamp) but sit left of "Part I: Slave". Proper fix: tag rules with
  their source sheet, apply only linked sheets (touches `SelectorEntry` + CSS cache format).
  Upstream-worthy, as is the clamp in `ParsedText::resolveFirstLineIndent`.
- KOSync verified end to end (register, push, smart pull) against stock koreader/kosync.
  Per-device `device_id` fix (12301d50) is upstream-worthy. Sync Behavior defaults to Smart
  (silent jump); "Ask Every Time" is the setting. No annotation sync in the protocol.
- Wi-Fi is per-task (network activities silent-reboot on exit to defragment heap), so there is
  no "associated" state to indicate. Keeping Wi-Fi up (background sync) would be a design change.
- 180-degree ghost frame after the post-Wi-Fi reboot: softInit now waits BUSY after the PSR soft
  reset; the log has shown `[GDEQ] soft reset busy for 1 ms` once, supporting the dropped-PSR
  theory. Watch for recurrence; `CMD:RESTART` reproduces the silent reboot.
- KOSync: throwaway server `podman run -d --rm --name kosync-test -p 17200:17200
  docker.io/koreader/kosync` → `http://192.168.8.95:17200`. KOSync syncs progress only, no
  annotations.

## 2. Remaining UI pass (item "2" — mostly done)
- Key legend overlaps full-screen images in the BMP viewer (draw it only where there's room, or
  reserve the band / hide it in image views).
- (done) Compact builds cap a block's combined CSS side insets at 1/6 of the column.
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
