# DeckPoint — next steps

Read `PROGRESS.md` first (dev loop, conventions). Current plan with rationale:
`/home/jtd/.claude/plans/abstract-weaving-pike.md` (Phase T hybrid touch UX, then round 2).

## 0. In progress — Round 2a (highlights & notes)
- Plan + research: `/home/jtd/.claude/plans/abstract-weaving-pike.md` (Round 2, incl. the
  "AnnotationSync v2.0.0 findings" appendix). User decisions: select by keyboard (`v` + hint
  labels) AND touch (long-press popup Look up/Highlight/Note); underline style; note editor =
  bottom sheet (~40% screen, page visible, Shift+Enter newline); 2a on-device + export, then 2b
  WebDAV sync with the user's Nextcloud (`/eBooks`) in AnnotationSync format.
- **Step 1 done** (205a626b): KOReader-exact XPointers + per-word offsets (section v55). Verified
  against 6 real KOReader highlights (samples in ~/booxreverse/koreader-samples/; env-gated test
  `KOReaderXPointer.MatchesRealKOReaderAnnotations`) and a live KOSync round trip Boox <-> T-Deck.
- **Step 2 done**: annotation model/store (`/.crosspoint/annotations/<md5>.json`, AnnotationSync
  shape), placement via the step-1 resolver, underline + note marker drawing, serial seeding
  (`CMD:ANNOTATE:<pos0>||<pos1>[||note]`, `CMD:ANNOTATE_TEST[:note]`). All six real KOReader
  highlights land on the same words on the T-Deck (incl. across paragraph and page breaks).
- **Step 3 done** (3f869d1c, checked on the glass by keyboard and touch): `v` labels pick start /
  end (Enter = one word), range inverted, Enter saves / `n` note / mic cancels; long-press popup
  Look up / Highlight / Note; tap (or `v` start) on a highlight opens Edit note / Delete / Look up
  (delete = tombstone). Code: reader/SelectionSession (test/selection), reader/EpubReaderSelection.cpp.
- **Step 4 done** (checked on the glass 2026-10-05: typing latency fine, edit/clear, discard
  confirm, touch Cancel/Save/tap-to-place, sheet placement top and bottom): note bottom sheet.
  Enter saves (empty removes the note), Shift+Enter / Alt+Enter new line, mic cancels (twice when
  changed), Alt+h/l/k/j cursor, Alt+Bksp clear, Alt+v help. One FAST refresh per idle panel (keys
  coalesce). Code: deckpoint/NoteEditor (test/note_editor), reader/EpubReaderNote.cpp. Known:
  Caps Lock makes Enter a newline; a page render or sleep while editing loses the text.
- **Step 5 done** (2026-10-05; list, reader-menu entry and export checked on the glass; note
  previews show the first line; the export locator drops the misleading TOC-position chapter number): `:notes` (`:n`) / reader menu "Notes" list
  (book order under chapter headings; j/k, Enter goes to the page, `e` goes there and opens the
  note sheet, `x` twice deletes, hold Enter / long-press: Go to / Edit note / Delete; tap goes to
  it) and `:export` (`/DeckPoint/notes/<title>.md` for Obsidian + `/DeckPoint/My Clippings.txt`,
  this book's old blocks replaced by streaming the file through a filter). Code:
  annotations/AnnotationExport (test/notes_export), reader/NotesListActivity, reader/NotesExport,
  reader/EpubReaderNotes.cpp. Bridge-mode screens now see letter keys via
  `Activity::onUnmappedKey()`. Clippings page/location/percent are estimates (spine bytes +
  visible offset when placed, chapter start otherwise; page = KOReader pageno when present).
- **Round 2a complete.** Next: **2b** (WebDAV sync with Nextcloud `/eBooks` in AnnotationSync
  format) built around the plan's "Sync paradigm: surviving time travel" rules (trust bit per
  timestamp, HTTP Date as clock source, stamp-at-sync, tombstones only from user deletes, dev
  seeds never upload, remote backup). Wipe the T-Deck's `/.crosspoint/annotations/` (test data)
  before the first real sync.
- **2b follow-ups** (2026-10-05):
  - Overlap rule (copied from AnnotationSync): touching/overlapping highlights collapse to the
    newer one. Make it lossless where it matters: when a merge drops the loser of an overlap and
    the loser has a note, append that note to the winner's note (marked as merged) — Boox sees an
    edited note, nothing written is lost. Plus: creating a T-Deck highlight that touches/overlaps
    an existing one offers "extend existing highlight" instead of making a second one.
  - Verified 2026-10-04: T-Deck -> Nextcloud -> desktop KOReader (v2026.07.1 + AnnotationSync
    v2.0.0, run headless on Xvfb :77 from the scratchpad, KO_HOME=scratchpad/kohome) draws the
    T-Deck highlight as an underline on the exact words; Boox -> Nextcloud -> T-Deck pulled 6.
  - Timestamps are local time without a zone (AnnotationSync format): the T-Deck's timezone was
    unset (UTC) so its edits looked ~4 h newer. Warn when annotation sync is on and no timezone
    is set; all devices must share one timezone.
  - SecureHttpClient treated unframed 204 as body-until-close (20 s stall, PUT reported failed
    though it succeeded): fixed (no-body statuses / HEAD). Watch other servers for similar quirks.
- **Modifier keys** (user got stuck with Alt locked): Alt never locks (double tap = one-shot);
  status-bar badge whenever Shift/Sym/Alt is latched or locked (state at BoardTDeckPro.cpp:450);
  also covers the note editor's Caps-Lock-makes-Enter-a-newline trap.
- **Theme pass** (user, 2026-10-05): Classic, Lyra Extended, RoundedRaff, Cover Grid look bad on
  240x320 (our tuning went into Lyra only). Screenshot every theme on each top-level screen via
  the bridge, list breakages, then fix or hide per theme with the user.
- **2b part B done** (WebDAV client + settings, untested on the glass): `deckpoint/sync/WebDavClient`
  (GET to SD / PUT from SD with If-Match or If-None-Match: * / PROPFIND Depth 0 / MKCOL; returns
  status, ETag and the `Date:` header), `dav::` URL/status/PROPFIND helpers (test/webdav),
  `AnnotationSyncStore` (`/.crosspoint/annotation_sync.json`, obfuscated + CRC'd password, web
  settings keys `as*`), Settings > System > Annotation Sync with Test connection. Part C: feed
  `Result::date` to the time-trust code; rclone's WebDAV ignores If-Match (always 201), Apache
  answers PUT without an ETag (client PROPFINDs it) and serves weak ETags right after a write.
- **2b part A done** (merge engine + time trust, host-tested, untested on the glass):
  `trustedtime::isCurrent()` (SNTP / HTTP Date this boot, or carried across a warm restart / deep
  sleep for <= 3 days via RTC_NOINIT) and `applyHttpDate()` (KOSync responses already feed it);
  `annotations/AnnotationMerge` (annotation_sweep.merge + plan rules 3-6, test/annotation_merge);
  `undated` / `localOnly` flags in the `<md5>.local.json` sidecar (CMD:ANNOTATE seeds are local-only);
  `AnnotationStore::mergeRemote / writeUpload / saveSyncSnapshot (<md5>.sync.json) / backupRemote*
  (<md5>.remote.bak)` for part C.
- **2b part C done** (sync orchestration, host-tested decisions, untested on the glass): `:sync` /
  reader-menu Sync / long-press / home-button Sync now run highlight sync first (when Settings >
  Annotation Sync is on and configured), then KOSync progress, in one Wi-Fi session; annotation
  sync alone also works without a KOSync account. `deckpoint/sync/AnnotationSync` (per book: GET
  `<folder>/<md5>.json` to `<md5>.remote.tmp`, Date header -> clock, abort "Clock not set" unless
  `isCurrent()`, backup before first upload / removing merge, `mergeRemote`, PUT `<md5>.upload.tmp`
  with If-Match / If-None-Match: *, MKCOL once on 409/404, one rerun on 412 then "Changed on server",
  snapshot) and `AnnotationSyncPlan` (test/annotation_sync). Result line under the progress result
  ("Highlights: +3 -1, uploaded"); a failed highlight sync stays on screen until a key. Serial:
  one `ASYNC Sync doc=... get=... put=... uploaded=y|n new_etag=... result=...` line per sync;
  `CMD:ANNOTATIONS_WIPE` deletes the open book's annotation files (main, flags, snapshot, backup,
  temporaries). Next: wipe, then the first real sync against Nextcloud + the Boox.
- Test rig: Boox Go 6 II over adb (KOReader F-Droid `org.koreader.launcher.fdroid`, AnnotationSync
  v2.0.0 → Nextcloud `/eBooks`, KOSync → this host). `adb shell input text` drops shifted chars;
  swipe up on the KOReader "." key for ':'. KOSync test server: `podman run -d --rm --name
  kosync-test -p 17200:17200 docker.io/koreader/kosync:latest` (ephemeral: re-register user
  `deckpoint` after restarts; creds in the session scratchpad only). Kavita may still run
  (`podman stop kavita-test`).
- Nits found 2026-10-04:
  - KOSync "Document Matching" defaulted to Filename on the T-Deck; default to Binary and
    probably hide Filename (user: filename matching is "a recipe for utter disaster").
  - Our KOSync percentage (0.75727) is lower than KOReader's (0.7582) for a position slightly
    *ahead* of it; KOReader may treat ours as behind. Align the percentage formula.
  - Wi-Fi Networks header: title and "N networks found" overtype at 240 px.
  - KOReader Sync settings: "Sync Server URL" label truncates to "Sync Server ...".
  - Reader page showed a leading space before a paragraph (" A new river…", Red Rising ch. 1);
    check whether it's text-indent rendering or a v55 regression.
  - Highlight ends are whole-word: "prisoners…we" (no space) underlines "we" too; KOReader stops
    after "…". Refine partial-word ranges when selection lands (step 3).
  - During a long `:search` the page stays on the B/W snapshot (no AA) under the progress line;
    re-render gray underneath when a search runs longer than a few seconds.
  - Seeded/imported highlights placed only in the chapter where they start (cross-chapter ranges).
  - `:sync` used to bounce to Home (fixed in 954a9beb, ActivityManager).

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

## 6d. Unified "Library server" (user, 2026-10-04)
- One setting instead of separate OPDS + KOSync pages: base URL + credentials/API key; detect the
  server type (Kavita, Calibre-Web-Automated, Grimmory/Booklore, plain koreader-sync-server, plain
  OPDS catalog) and derive both endpoints. E.g. Kavita: OPDS `/api/opds/<key>`, KOSync
  `/api/koreader/<key>`. Round-2 annotation sync hangs off the same entry. Keep the existing
  separate settings as the "custom" fallback. Tested so far: Gutenberg OPDS (works, read the
  Odyssey), Kavita OPDS added via the web UI OPDS page (podman `kavita-test`, library in
  scratchpad/kavita, books must sit in per-series folders). Web UI nit: OPDS Save gives no visible
  confirmation.

## 7. Docs & credits (user requirement)
- README section crediting **CrossPoint Reader** and **CrossInk** as inspirations (not just code
  sources), plus FreeInk SDK, Meshtastic, LilyGo; `CREDITS.md` mapping pieces to origins.
