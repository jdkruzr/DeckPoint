# Installing DeckPoint

## What you need

- A LilyGo **T-Deck Pro** (ESP32-S3, 3.1" e-ink) and a USB-C **data** cable.
- A microSD card with your books (EPUB) on it. DeckPoint keeps its settings, caches and
  highlights on the card under `/.crosspoint/`, so the card is where your stuff lives.
- From the [latest release](https://github.com/jdkruzr/DeckPoint/releases/latest):
  **`deckpoint-tdeckpro-full.bin`** (the first-install image).

Flashing replaces the firmware that shipped on the device (LilyGo's factory demo or
Meshtastic). It does not touch the SD card. To go back later, flash that firmware again
(LilyGo publishes it in their T-Deck-Pro repository).

## First install

The full image contains the bootloader, partition table and DeckPoint; it is written at
address **0x0**.

### Option A: in the browser (Chrome or Edge)

1. Plug the T-Deck Pro into your computer.
2. Open <https://espressif.github.io/esptool-js/>, click **Connect** and pick the device's
   serial port (often "USB JTAG/serial debug unit").
3. Under *Program*, set **Flash Address** to `0x0`, choose `deckpoint-tdeckpro-full.bin`, click
   **Program** and wait for it to finish (about a minute).
4. Press the **lower side button** (reset). DeckPoint starts.

### Option B: esptool on the command line

```bash
pip install esptool
esptool --chip esp32s3 write-flash 0x0 deckpoint-tdeckpro-full.bin
```

(With esptool older than v5 the command is spelled `write_flash`.) esptool resets the device
when it is done.

### If the port does not show up

Put the ESP32-S3 into download mode: **hold the upper side button, press and release the lower
side button, then let go of the upper one.** Flash again, then press the lower button to boot.
Also try another cable; plenty of USB-C cables are charge-only.

## First steps

- `?` on any screen lists the keys that work there. In a book: `j`/`k` (or Space) turn pages,
  `:` opens the command line (`:toc`, `:notes`, `:sync`, ...), `v` starts a highlight, `d`
  looks a word up.
- The **upper side button** sleeps and wakes the device; the lower one resets it.
- The mic key is Back.

### Optional setup

- **Dictionary**: copy a StarDict dictionary (one per folder, uncompressed `.idx`) to
  `/dictionaries/<name>/` on the SD card, then pick it in Settings → Reader → Dictionary.
  Details: [docs/dictionary.md](../dictionary.md).
- **Wi-Fi**: Settings → System → Wi-Fi Networks. Wi-Fi is only on while a network task runs
  (sync, File Transfer, OPDS, updates).
- **Time zone**: Settings → System → Clock. Set it before using annotation sync; every
  device that syncs highlights has to use the same time zone.
- **Reading progress sync (KOSync)**: Settings → System → KOReader Sync.
- **Highlight sync with KOReader**: Settings → System → Annotation Sync. Point it at a WebDAV
  folder (e.g. Nextcloud `https://<host>/remote.php/dav/files/<user>/<folder>`, with an app
  password) — the same folder KOReader's AnnotationSync plugin uses. `:sync` in a book syncs
  highlights and progress together.
- **Getting books on**: File Transfer → USB Drive (the card shows up on your computer; eject
  it there to return), or the File Transfer web page / WebDAV drive over Wi-Fi.

## Updating

- **Over Wi-Fi**: Settings → System → Check for Updates. It installs
  `deckpoint-tdeckpro.bin` from the latest release. (This needs the release to be publicly
  downloadable.)
- **Over USB**: flash the new `deckpoint-tdeckpro-full.bin` exactly as above. Settings,
  progress and highlights are on the SD card and survive.

## Xteink X4 Pro

The release also has `deckpoint-x4pro.bin` / `deckpoint-x4pro-full.bin`. DeckPoint keeps the
X4 Pro build compiling, but it is not tested on hardware by us; if you run an X4 Pro, upstream
[CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) is the safer choice.
