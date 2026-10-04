# DeckPoint — next steps

Read `PROGRESS.md` first (dev loop, conventions). Current plan with rationale:
`/home/jtd/.claude/plans/abstract-weaving-pike.md` (Phase T hybrid touch UX, then round 2).

## 0. In progress — Phase T steps 3-4 (touch in the reader, lists, help, hint tags)
- Steps 1-2 done (2fea3c0f). Steps 3-4 were being implemented by an agent at the end of session 3:
  T-Deck defaults TAP_AND_SWIPE + center-tap menu (with a one-time migration if needed), long-press
  word = instant lookup, keyboard modes take priority over touch, full-pitch list hit areas, touch
  checks on every screen, a Touch section in `?` help, hint tags moved above words. If a
  `wip/touch-reader` branch exists, resume from it; otherwise check `git status` for its edits.
- Then: on-glass round 3 (real use) and commit.

## 1. Done this round (upstream candidates)
- Stylesheet scoping per document (5f80f808), hanging-indent clamp (bef53e2d), book center/right
  alignment kept under the user's alignment setting (d6e959a0), per-device KOSync id (12301d50).
  Caveat: right-aligned blocks in RTL books are only recognized when the element itself
  declares `direction: rtl`. Package as upstream PRs at some point.
- KOSync verified end to end (register, push, smart pull) against stock koreader/kosync.
  Per-device `device_id` fix (12301d50) is upstream-worthy. Sync Behavior defaults to Smart
  (silent jump); "Ask Every Time" is the setting. No annotation sync in the protocol.
- Annotation sync research (2026-10-03): KOSync protocol/servers are progress-only (confirmed);
  KOReader syncs annotations via plugins - AnnotationSync.koplugin (WebDAV) and Readest (own
  server, CFI<->XPointer bridge). KOReader stores EPUB annotations as XPointers (pos0/pos1 + text,
  note, chapter, datetime, color) in .sdr Lua sidecars - the same position format CrossPoint
  already produces. OPDS Progression 1.0 (draft) covers progress only. Round-2 proposal: store
  KOReader-shaped annotations (ids, timestamps, tombstones), sync via AnnotationSync's WebDAV
  format (read its source first), export Markdown / My Clippings.txt / Readwise JSON.
  WebDAV client needs no library: SecureHttpClient::sendRequest(method, body) (any verb, TLS via
  wolfSSL) + expat for PROPFIND XML; ~300-400 LOC, ~5-10 KB flash; TLS handshake ~35 KB transient
  (MIN_FREE_FOR_TLS). CrossPoint's WebDAVHandler.cpp is a server (File Transfer: mount the SD as a
  network drive), and PluginCatalogActivity already browses XML listings (207 Multi-Status) and
  downloads with Basic auth - a partial WebDAV client. Round-2 design is in the plan file.
- Docs: tell users they can mount the device as a WebDAV drive while File Transfer is open.
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
- (done) File Browser compact rows (321597fc); File Transfer URL-first screen (f72616cf);
  Library single legend (526c9b1b; "k: search" no longer fits on the tab-strip legend).
  Possible: show the highlighted file's full name somewhere in the File Browser. Reader menus toured (425817e0); still to check: Look Up with a StarDict dictionary
  on the SD (without one it just bounces back), Text Settings preview says "14 pt" while the
  compact font is 17 px.
- Web UI (Home -> File Transfer -> Join Network / Create Hotspot; `src/network/CrossPointWebServer.cpp`):
  device screen done; still try the browser side end to end (uploads, settings, fonts, OPDS,
  Wi-Fi, plugins). mDNS name is still crosspoint.local. Likely the nicest way to load books.
- `KeyboardEntryActivity` legend strings are English literals (fine for now; i18n later).
- Remove `-DDECKPOINT_KEY_DEBUG` from release builds eventually.

## 3. Grayscale follow-ups
- Done 2026-10-03: two calibrations picked by the base refresh (text 3/1 after a fast base,
  images 4/2 after a deep base); image viewer uses a deep base and no Loading popup (ghosts);
  RefreshMode::Full is now the standard OTP "deep clean" (~3.1 s); CMD:CLEAN, CMD:GRAYIMG.
  Test cards must be paletted at exactly 0/85/170/255, or CrossPoint's photo pipeline
  (adjustPixel boost + Atkinson) dithers the bands. Ornament "barcode" is the real image.
- Gray card v3: add an error-diffused 4-level gradient (current card's gradient is a staircase).
- Finer gray steps if needed: shorten the frame time (UC8253 PLL/frame-rate register) so dark
  can land between 1 and 2 of today's frames.
- Covers / sleep-screen images in gray: check on glass (cover sleep screen uses which base?).
- Reader pages with images: base is fast, so images there use the text profile; check.
- AA toggle "off" mode (user, 2026-10-03): Text Settings > Style > Text Anti-Aliasing exists, but
  "off" thresholds 2-bit glyphs (bloats them). Make "off" use FreeType mono hinting instead (as
  the crisp compact UI fonts do): runtime for TTF, pre-generated --mono sets for built-ins.
- Font contest with AA done 2026-10-03 via SD .ttf (`/.fonts/<Family>/`): user picked **Source
  Sans 3** for reading (Fira Sans close second; Plex crisp/cold, Atkinson heavy, Inter neutral).
  Its narrow space (200/1000 em) is by design. TTF page cost ~+100 ms first render (glyph raster).
  Possible: bundle Source Sans 3 as the compact built-in sans (replacing Noto Sans) so fresh
  installs default to it; serif contenders (Literata, Source Serif 4) not yet tried.

## 4. Keyboard-native reader (plan Phase 4 — user wants to feel 2 & 3 first)
- Vim keys (j/k/space/b, counts, gg/G, ]]/[[, `/` search with n/N, marks), command palette home
  (`:` commands, `#` chapters, `%` go-to over `LibraryIndex`), hint-mode dictionary (`d` labels),
  highlights & notes (port CrossInk's clippings stack + typed note field), CrossInk quick actions /
  stats / dictionary extras (see plan Phase 4b).

## 5. Hardware / power
- **Wake on keypress**: OR `BoardTDeckPro::keyboardWakeMask()` (GPIO15, RTC-capable) into the ext1
  deep-sleep wake mask and call `BoardTDeckPro::prepareForSleep()` before sleeping (not wired yet).
- Bluetooth keyboards / page-turner remotes: SDK already has `freeink-sdk/libs/network/BleKeyboardHost`
  (~1.5k LOC NimBLE HID host, emits the same KeyEvent as the built-in keyboard, raw button
  learning for remotes). Wire-up: set FREEINK_CAP_BLE_KEYBOARD + add h2zero/NimBLE-Arduino to
  tdeckpro lib_deps, merge its key queue into HalKeyboard, Settings > Bluetooth (scan/pair/forget).
  Costs: a few hundred KB flash, ~40-60 KB heap while on, radio power, shared 2.4 GHz with Wi-Fi.
- (done) Side buttons: upper = BOOT/GPIO0 (sleep/wake), lower = reset (EN), confirmed 2026-10-04.
- Touch word actions (user, 2026-10-02): long-press a word -> popup offering both dictionary
  lookup and annotate/highlight (one entry point for both, shared with hint mode's word targets).
- Touch (v1.1 CST3530 @0x1A, IRQ-driven; SDK lacks this controller) — user flagged as coming.
  Revisit `hasTouch()`-gated layouts (`hidesButtonHints()` already distinguishes keyboard vs touch).
- Haptics (DRV2605 on v1.1), light sensor, gyro tilt, LoRa quote sharing (plan Phase 5).

## 6. Temperature-compensated gray (plan Phase 6)
- Read BQ27220 battery temperature before each gray pass; scale light/dark frames from a small
  table; calibrate with gray-card photos at ~2 extra temperatures. (UC8253's own sensor needs a
  read-capable bus.) No CrossPoint/CrossInk/SDK code compensates custom LUTs today.

## 6b. Flash diet (plan Phase 7)
- Measured: code 2.1 MB, fonts 1.5 MB, hyphenation 374 KB, i18n 366 KB, web UI 120 KB.
  Offload candidates: extra reader fonts (~1.1 MB), non-English hyphenation (~350 KB) and
  translations (~340 KB). Cheapest win: unused 3.4 MB `spiffs` partition -> bigger app slots.
  Try PSRAM direct .ttf loading (also speeds up the AA font contest).

## 6c. Release plumbing
- OTA updater still targets crosspoint-reader GitHub releases (`src/network/OtaUpdater.cpp`);
  point it at jdkruzr/DeckPoint release assets (board tag `tdeckpro`) or disable it.
- Branding: user-visible names are DeckPoint (boot/sleep logo + text, hotspot SSID, mDNS
  `deckpoint.local`, DHCP hostname, web UI titles). Internals (`/.crosspoint/`, class names,
  `window.CrossPoint` plugin API, Calibre plugin protocol, service URLs) intentionally unchanged.

## 7. Docs & credits (user requirement)
- README section crediting **CrossPoint Reader** and **CrossInk** as inspirations (not just code
  sources), plus FreeInk SDK, Meshtastic, LilyGo; `CREDITS.md` mapping pieces to origins.
