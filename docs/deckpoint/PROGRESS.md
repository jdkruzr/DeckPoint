# DeckPoint — progress log

DeckPoint is a keyboard-first fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)
for the **LilyGo T-Deck Pro** (ESP32-S3, 16MB flash, 8MB quad PSRAM, GoodDisplay GDEQ031T10
3.1" 240x320 e-ink on a UltraChip UC8253, TCA8418 4x10 keyboard, BQ27220 gauge, SPI SD).
Branch `deckpoint`, base upstream commit `f331030f`. Inspirations and code sources:
CrossPoint Reader and CrossInk (credit both prominently in user-facing docs).

Last updated: 2026-10-04 (session 4).

## Repo layout & conventions
- Monorepo: `freeink-sdk/` is **vendored** (was a submodule), base `bbd528c`
  (`freeink-sdk/VENDORED.md`). Remotes: `upstream-reader`, `upstream-sdk`.
- DeckPoint-owned code: `src/deckpoint/`, `freeink-sdk/libs/hardware/{BoardTDeckPro,KeyMatrix}`,
  `lib/hal/HalKeyboard.*`, `tools/fontlab/`, `scripts/deckpoint_*`, `scripts/gen_deckpoint_icons.py`.
- Edits to upstream files are small and marked `// DECKPOINT:`. Compact-screen behaviour is
  gated on `DECKPOINT_COMPACT_UI` (compile time), keyboard behaviour on `FREEINK_CAP_KEYBOARD`
  / `halKeyboard.present()`.
- Build env: `pio run -e tdeckpro` (flags in `platformio.ini`: `FREEINK_DEVICE_TDECKPRO`,
  `DECKPOINT_COMPACT_UI=1`, `DECKPOINT_KEY_DEBUG`, `FREEINK_CAP_USB_MSC`,
  `USE_BLOCK_DEVICE_INTERFACE`, `qio_qspi`). Stock `x4pro` env still builds (checked).
- Licenses: CrossPoint, CrossInk, FreeInk SDK = MIT. Meshtastic, LilyGo T-Deck-Pro repo and
  bb_epaper = **GPL-3 → reference only, never copy code**.

## Dev loop (how to work on the device)
- Device: `/dev/ttyACM0` (re-check `ls /dev/ttyACM*`). Unit is a **v1.1** (DRV2605 present),
  no front light. Side buttons: **upper ("volume up") = BOOT/GPIO0** = sleep and wake;
  **lower ("volume down") = chip reset (EN)**, which also "wakes" by rebooting.
- `scripts/deckpoint_flash.sh <dir> <port>`: build + stop bridge + flash.
- `scripts/deckpoint_serial.py daemon --port /dev/ttyACM0 --dir <dir>` (run in background;
  restart after each flash): logs to `<dir>/serial.log`; commands via
  `deckpoint_serial.py send --dir <dir> ...`:
  - `screenshot <name>` → `<dir>/<name>.png` (framebuffer; matches glass for B/W, verified by photo)
  - `keys j j enter` (tokens: chars, `enter bksp esc space shift sym alt`)
  - `raw CMD:...` firmware serial commands: `CMD:SCREENSHOT`, `CMD:KEY:<code>`,
    `CMD:BOARD` (rev/keyboard/LoRa status), `CMD:TESTPATTERN` (calibration image),
    `CMD:GRAY:<light>,<dark>,<repeat>` (live gray-waveform tuning)
- Opening the serial port can reset the device. The user can photograph the glass for things
  screenshots can't show (gray tones, glass-only artifacts).
- Getting files onto the SD: Home → File Transfer → USB Drive; card mounts at
  `/run/media/jtd/00D8-D230`; `udisksctl unmount -b /dev/sda1 && udisksctl power-off -b /dev/sda`.

## Done
### Board bring-up (SDK side)
- `BoardConfig.h`: `FREEINK_DEVICE_TDECKPRO`, `TDECK_PRO` profile (landscape 320x240 framebuffer,
  SD on the shared SPI bus, BQ27220+BQ25896 gauge on I2C 13/14, BOOT as power), `FREEINK_CAP_KEYBOARD`.
- `Uc8253Gdeq031Driver`: OTP waveforms with GxEPD2's forced-temperature trick (full ~1.0 s, fast
  ~0.65 s), soft-reset init, no deep sleep without a RESET line (v1.0), transposes the landscape
  framebuffer into portrait controller RAM (orientation verified with the test pattern + photo).
- `BoardTDeckPro`: shared-SPI CS parking, GPS/modem/motor rails off, **SX1262 put to sleep**
  (~160 nA; it stays powered because LORA_EN feeds its whole supply), v1.0/v1.1 detection
  (EPD RST 16 on v1.1), keyboard.
- Keyboard (`KeyMatrix` + `BoardTDeckPro`): our own TCA8418 driver (written from the TI datasheet),
  interrupt line GPIO15 (verified on the schematic), full keymap incl. Sym layer, Shift/Sym/Alt
  chord + one-shot + lock, mic key = Esc, KeyEvent queue, button bridge (Enter/⌫/Esc/hjkl/Space)
  with debounce-safe pulses, raw mode, `injectKey`, key activity resets the sleep timer.
  `KeyEvent` is shared with `BleKeyboardHost`.

### Reader/app side
- `HalKeyboard` HAL; `Activity::wantsRawKeys()/onKey()`; ActivityManager routes keys.
- Physical typing in `KeyboardEntryActivity` (search, Wi-Fi, OPDS): insert, ⌫/Shift+⌫, Enter,
  Esc, Alt+H/L cursor, Alt+P password reveal, Alt+⌫ clear; on-screen keyboard hidden.
- **USB Drive** over the SPI card (relaxed HalStorage's SDMMC-only guard); USB Drive now marks
  the library index dirty (upstream bug: books copied over USB were invisible).
- **Compact UI** (`src/deckpoint/CompactMetrics.h`, theme tables wrapped in
  `DECKPOINT_THEME_METRICS`): smaller metrics, no button-hint strips, inverted (non-dithered)
  selections everywhere, text-only Home menu aligned to the content edge, outline-style recent
  book card, 24px list icons, 12px checkboxes, 16px search icon, compact tabs without dither.
- **Fonts**: `fontconvert.py` gained `--mono` (FreeType mono hinting, crisp 1-bit) and `--ppem`
  (pixel sizing). `convert-deckpoint-compact-fonts.sh` generates UI Ubuntu 15/12/11 px (mono)
  and reader Noto Serif/Sans 15/17/19/21 px (2-bit) registered into the stock font slots
  (`src/deckpoint/CompactReaderFonts.h`). Stock fonts not linked on compact builds
  (flash 90.9% → 78%). Font choice made on-glass with `tools/fontlab` (Ubuntu won).
- **Key legends everywhere**: themes' `drawButtonHints()` render the screen's own labels as a key
  legend (`src/deckpoint/KeyLegend.*`, Esc shown with the mic glyph), tab lists add `h/l: tab`,
  Library has a focus-aware legend, Settings > Display > **Key Legend** toggle (default on).
- Tab lists: `h`/`l` switch tabs, Enter on the strip enters the list (Library, Settings).
- Reader: compact defaults (hyphenation on, 10 px margins), status bar fits its band and no longer
  collides with descenders. Page turn ~780 ms; 12.5 MB EPUB opens in ~11 s cold; heap low-water
  ~174 KB of 294 KB, PSRAM untouched.
- **Grayscale (first ever for this panel, as far as we know)**: overlay masks + register-LUT
  "nudge" (VSL toward white for N frames; ~20 ms/frame; repeat bytes must be ≥ 1 or the controller
  stops). Default light=2/dark=1 frames: clean solid mid-gray on images, good anti-aliased text
  (photo-verified). Tunable live with `CMD:GRAY`.

### Tooling
- `scripts/deckpoint_serial.py`, `scripts/deckpoint_flash.sh`, `scripts/gen_deckpoint_icons.py`,
  `tools/fontlab/` (specimen + on-glass BMP pages), `CMD:TESTPATTERN`, gray test card
  (4 bands + gradient; regenerate with PIL, copy via USB Drive).

## Session 2 (2026-10-02 night -> 10-03)
- EPUB layout (upstream bugs, PR candidates): hanging-indent clamp (bef53e2d); per-document
  stylesheet scoping with @import (5f80f808, CSS cache v13); book center/right alignment kept
  under the user's alignment setting (d6e959a0). Section format now v53.
- Compact UI: nested CSS margin cap (1/6 column); inverted focus instead of dither; key legend
  clears its band (no popup overprint); single Library legend; File Browser one-line rows,
  no icons, `ls -F` folders; File Transfer URL-first; images fit above the legend.
- Keyboard: typed Go to %; Text Settings Enter enters list; "Hold Enter for actions".
- KOSync verified end to end; per-device hashed `device_id`, device name DeckPoint (12301d50).
- Dictionary: StarDict en-simple on SD (`/dictionaries/en-simple/`); IPA glyphs in reader fonts.
- Branding: DeckPoint name + logo on boot/sleep, SSID, mDNS, DHCP, web UI (dd800755).
- Display: GDEQ031 soft-reset BUSY wait (180-degree ghost theory); deep-clean Full refresh;
  text 3/1 and image 4/2 gray profiles, photo-calibrated (e9a28515). CMD:CLEAN/GRAYIMG/RESTART.
- Tooling: glass photos arrive via ntfy (see memory `glass-photos-ntfy`); repo pushed to
  https://github.com/jdkruzr/DeckPoint (private). Bridge daemon: run with the 2 h timeout.

## Session 3 (2026-10-03)
- Fonts: SD .ttf fonts sized like built-ins (pt+3 px) with light hinting (d4ad3a57); user reads in
  Source Sans 3 (`/.fonts/SourceSans3`). LoRa re-sleep after warm restarts (dd3f0bc1).
- Images: reader image pages use the deep base + image gray profile (204b8a76); per-image
  auto-levels before dithering (b9aa1d13, upstream candidate).
- Keyboard reader round 1 complete: global `?` help on every screen (3bc38d05), vim keys + marks
  (08990a60), `:` command line + registry (508be0ad), `d` hint lookup (96e77c7e), `/` search with
  n/N (335a56de). Home legend shows ?: help; Home help has a keyboard primer.
- Touch (Phase T): CST3530 driver + CMD:TOUCHTEST (49b8348f), calibration rounds 1-2 passed on the
  glass; touch input split from touch-first layout + Controls > Touchscreen toggle + per-board
  gesture mask + small-screen thresholds (2fea3c0f).
- Tooling: glass photos stream in via an ntfy listener (memory `glass-photos-ntfy`); LAN exempt from
  ntfy rate limits; keyboard charset rule saved (memory `tdeck-keyboard-charset`).

## Session 4 (2026-10-04)
- Phase T touch complete: reader tap zones + swipes (T-Deck defaults Tap+Swipe, center-tap menu,
  one-time settings migration), long-press word = lookup, keyboard modes win over touch, full-pitch
  list tap targets, Touch section in ? help, hint tags above words (cd799413). Overlays re-render
  gray pages on close so AA survives (82c07441). Hotspot hints wrap.
- Legends: mic glyph, " | " separators, "?: Help" always kept; Alt+v also opens help; help pages
  show the mic icon and page back correctly; Controls hides button-only settings (cd799413,
  a4a34d0e, 53829855). Side buttons confirmed: UPPER = BOOT/GPIO0 sleep+wake, LOWER = reset.
- OPDS verified: Gutenberg (read the Odyssey) and a throwaway Kavita (podman `kavita-test`,
  OPDS at http://192.168.8.95:5000/api/opds/<key>, added via the web UI OPDS page).
- Round 2 (highlights/notes/annotation sync) planned and approved: see the plan file
  `/home/jtd/.claude/plans/abstract-weaving-pike.md` (design, AnnotationSync + KOReader spec).

## Session 5 (2026-10-04 -> 10-05): highlights, notes, KOReader-compatible sync
- **Round 2a complete** (all checked on the glass): KOReader-exact XPointers + per-word offsets
  (205a626b, verified against 6 real KOReader highlights and a live KOSync round trip), annotation
  store + underline drawing (2e247436), selection by `v` labels / long-press popup (3f869d1c),
  note editor bottom sheet (7d176147), `:notes` list + `:export` Markdown/My Clippings (d75f1009).
- **Round 2b complete** (9ed88e80): `:sync` / reader-menu Sync syncs progress + highlights through
  the user's Nextcloud (`/eBooks`) in AnnotationSync's format; verified Boox -> Nextcloud -> T-Deck
  and T-Deck -> Nextcloud -> KOReader (highlight drawn on the exact words). Time-travel-safe merge
  rules (plan file, "Sync paradigm"). SecureHttpClient 204 fix.
- **Cleanup batch** (490e051b): Alt never locks + CAPS/SYM/ALT badge; Clock settings on RTC-less
  boards + timezone warning; Filename matching removed everywhere; overlap merges keep notes;
  Extend highlight; KOReader-style percentage; wrapped centered messages; reader-menu Sync moved up.
- Fixes found by testing: `:sync` bounced to Home (954a9beb, ActivityManager), seeds' chapter
  (b40b8c61).
- **Test rigs** (session scratchpad `/tmp/claude-39601104/-home-jtd-DeckPoint/<session>/scratchpad`):
  - Desktop KOReader v2026.07.1 + AnnotationSync v2.0.0 on Xvfb `:77` (`koreader/`, `kohome/`
    = KO_HOME with settings.reader.lua pointing at Nextcloud; creds from
    `~/.config/deckpoint/nextcloud`, mode 600). Launch: `env -u WAYLAND_DISPLAY DISPLAY=:77
    SDL_VIDEODRIVER=x11 KO_HOME=... ./bin/koreader <book>` (without `env -u WAYLAND_DISPLAY` it
    opens on the user's real desktop). Drive with `ko/tap.sh x y`, `ko/shot.sh name`.
  - Boox Go 6 II over adb (KOReader uninstalled by the user since; AnnotationSync source kept in
    `~/booxreverse/AnnotationSync.koplugin-v2.0.0`, samples in `~/booxreverse/koreader-samples/`).
  - KOSync test server: `podman run -d --rm --name kosync-test -p 17200:17200
    docker.io/koreader/kosync:latest` (ephemeral; user `deckpoint`, creds in scratchpad
    `kosync-test-creds.txt`). Kavita test server `kavita-test` may still run.
  - Theme survey: `themes/survey.sh <uiTheme> <tag>` + `themes/sheet.py <tag>` (contact sheets),
    using the dev serial command `CMD:SET:<key>=<int>` (sets a setting by web key, saves,
    restarts to Home).
- USB Drive mode ignores mic while the host has the card mounted (looked frozen); eject on the
  host (`udisksctl unmount -b /dev/sda1; udisksctl power-off -b /dev/sda`) releases it.
