# DeckPoint — progress log

DeckPoint is a keyboard-first fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)
for the **LilyGo T-Deck Pro** (ESP32-S3, 16MB flash, 8MB quad PSRAM, GoodDisplay GDEQ031T10
3.1" 240x320 e-ink on a UltraChip UC8253, TCA8418 4x10 keyboard, BQ27220 gauge, SPI SD).
Branch `deckpoint`, base upstream commit `f331030f`. Inspirations and code sources:
CrossPoint Reader and CrossInk (credit both prominently in user-facing docs).

Last updated: 2026-10-02 (end of first session).

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
  no front light. Side **"volume down" button = BOOT/GPIO0** = power/wake (hold ~1 s).
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
