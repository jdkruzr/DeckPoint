#!/usr/bin/env python3
"""DeckPoint serial bridge: headless log + screenshots + key injection.

Holds the device's serial port open (opening a USB-Serial/JTAG port can reset
the ESP32-S3, so we open it once and keep it), appends everything the device
prints to a log file, and accepts commands from a FIFO:

    screenshot [name]        save the framebuffer as <shots>/<name>.png
    keys <tokens...>         tap keys on the T-Deck Pro keyboard, in order
    raw <line>               send a raw line (e.g. CMD:SCREENSHOT)
    quit                     close the port and exit

Key tokens are single characters (a-z, 0-9 and the symbols printed on the
keycaps, uppercase letters via a one-shot Shift) or names: enter, bksp, esc,
space, shift, sym, alt. Separate taps are paced so the UI can react.

Usage:
    scripts/deckpoint_serial.py daemon --port /dev/ttyACM0 --dir /tmp/deckpoint
    scripts/deckpoint_serial.py send --dir /tmp/deckpoint keys j j enter
    scripts/deckpoint_serial.py send --dir /tmp/deckpoint screenshot home

The daemon reconnects by itself after a flash/reset re-enumerates the port.
Stop it (send quit) before flashing so esptool can have the port.
"""

import argparse
import os
import sys
import threading
import time
from pathlib import Path

import serial  # pyserial

# TCA8418 matrix codes (BoardTDeckPro.cpp KEYMAP index + 1).
BASE_CODES = {
    "q": 10, "w": 9, "e": 8, "r": 7, "t": 6, "y": 5, "u": 4, "i": 3, "o": 2, "p": 1,
    "a": 20, "s": 19, "d": 18, "f": 17, "g": 16, "h": 15, "j": 14, "k": 13, "l": 12,
    "z": 29, "x": 28, "c": 27, "v": 26, "b": 25, "n": 24, "m": 23, "$": 22,
}
NAMED_CODES = {
    "bksp": 11, "enter": 21, "alt": 30, "rshift": 31, "sym": 32, "space": 33,
    "esc": 34, "mic": 34, "shift": 35, "lshift": 35,
}
SYM_CHARS = {
    "@": "p", "+": "o", "-": "i", "_": "u", ")": "y", "(": "t", "3": "r", "2": "e", "1": "w", "#": "q",
    '"': "l", "'": "k", ";": "j", ":": "h", "/": "g", "6": "f", "5": "d", "4": "s", "*": "a",
    ".": "m", ",": "n", "!": "b", "?": "v", "9": "c", "8": "x", "7": "z", "0": "mic",
}
SHIFT, SYM = NAMED_CODES["shift"], NAMED_CODES["sym"]


def token_to_codes(tok: str) -> list[int]:
    if tok in NAMED_CODES:
        return [NAMED_CODES[tok]]
    if len(tok) != 1:
        raise ValueError(f"unknown key token: {tok!r}")
    if tok in BASE_CODES:
        return [BASE_CODES[tok]]
    if tok == " ":
        return [NAMED_CODES["space"]]
    if tok.isalpha() and tok.lower() in BASE_CODES:
        return [SHIFT, BASE_CODES[tok.lower()]]
    if tok in SYM_CHARS:
        key = SYM_CHARS[tok]
        return [SYM, NAMED_CODES.get(key) or BASE_CODES[key]]
    raise ValueError(f"no key produces {tok!r}")


class Bridge:
    def __init__(self, port: str, workdir: Path, width: int, height: int):
        self.port = port
        self.dir = workdir
        self.width, self.height = width, height
        self.log = open(workdir / "serial.log", "ab", buffering=0)
        self.ser: serial.Serial | None = None
        self.lock = threading.Lock()
        self.pending_shot: str | None = None
        self.running = True

    # --- port management ------------------------------------------------------
    def connect(self) -> None:
        while self.running:
            try:
                s = serial.Serial()
                s.port, s.baudrate, s.timeout = self.port, 115200, 0.2
                s.dtr = False
                s.rts = False
                s.open()
                self.ser = s
                self.note(f"connected {self.port}")
                return
            except (serial.SerialException, OSError):
                time.sleep(0.5)

    def note(self, msg: str) -> None:
        self.log.write(f"#### [bridge {time.strftime('%H:%M:%S')}] {msg}\n".encode())

    def write_line(self, line: str) -> None:
        with self.lock:
            if self.ser is None:
                return
            try:
                self.ser.write((line + "\n").encode())
                self.ser.flush()
            except (serial.SerialException, OSError):
                pass

    # --- reader -----------------------------------------------------------------
    def reader(self) -> None:
        buf = b""
        shot: bytearray | None = None
        shot_len = 0
        while self.running:
            if self.ser is None:
                self.connect()
                continue
            try:
                chunk = self.ser.read(8192)
            except (serial.SerialException, OSError):
                self.note("port lost; reconnecting")
                try:
                    self.ser.close()
                except Exception:
                    pass
                self.ser = None
                time.sleep(0.5)
                continue
            if not chunk:
                continue
            buf += chunk
            while True:
                if shot is not None:
                    need = shot_len - len(shot)
                    take = buf[:need]
                    shot.extend(take)
                    buf = buf[len(take):]
                    if len(shot) < shot_len:
                        break
                    self.save_shot(bytes(shot))
                    shot = None
                    continue
                nl = buf.find(b"\n")
                if nl < 0:
                    break
                line, buf = buf[: nl + 1], buf[nl + 1 :]
                text = line.decode("utf-8", "replace").strip()
                if text.startswith("SCREENSHOT_START:"):
                    shot_len = int(text.split(":", 1)[1])
                    shot = bytearray()
                    continue
                if text == "SCREENSHOT_END":
                    continue
                self.log.write(line)

    def save_shot(self, data: bytes) -> None:
        from PIL import Image  # system python has Pillow

        name = self.pending_shot or time.strftime("shot-%H%M%S")
        self.pending_shot = None
        if len(data) != self.width * self.height // 8:
            self.note(f"screenshot size {len(data)} != {self.width}x{self.height}/8; saved raw")
            (self.dir / f"{name}.raw").write_bytes(data)
            return
        img = Image.frombytes("1", (self.width, self.height), data)
        # Framebuffer is landscape; the UI is held portrait (CrossPoint's
        # Portrait orientation rotates it the same way).
        img = img.transpose(Image.Transpose.ROTATE_270)
        path = self.dir / f"{name}.png"
        img.save(path)
        self.note(f"screenshot saved {path}")

    # --- commands ----------------------------------------------------------------
    def handle(self, cmd: str) -> None:
        parts = cmd.strip().split(" ", 1)
        if not parts or not parts[0]:
            return
        verb, rest = parts[0], (parts[1] if len(parts) > 1 else "")
        if verb == "screenshot":
            self.pending_shot = rest.strip() or None
            self.write_line("CMD:SCREENSHOT")
        elif verb == "keys":
            tokens = rest.split()
            for tok in tokens:
                try:
                    codes = token_to_codes(tok)
                except ValueError as e:
                    self.note(str(e))
                    continue
                for code in codes:
                    self.write_line(f"CMD:KEY:{code}")
                    time.sleep(0.12)
                time.sleep(0.6)  # let the e-ink refresh before the next tap
            self.note(f"keys done: {rest}")
        elif verb == "raw":
            self.write_line(rest)
        elif verb == "quit":
            self.running = False
        else:
            self.note(f"unknown command: {cmd.strip()}")

    def command_loop(self) -> None:
        fifo = self.dir / "cmd"
        if not fifo.exists():
            os.mkfifo(fifo)
        while self.running:
            with open(fifo, "r") as f:  # blocks until a writer appears
                for line in f:
                    self.handle(line)
                    if not self.running:
                        break


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="mode", required=True)
    d = sub.add_parser("daemon")
    d.add_argument("--port", default="/dev/ttyACM0")
    d.add_argument("--dir", default="/tmp/deckpoint")
    d.add_argument("--width", type=int, default=320)
    d.add_argument("--height", type=int, default=240)
    s = sub.add_parser("send")
    s.add_argument("--dir", default="/tmp/deckpoint")
    s.add_argument("command", nargs=argparse.REMAINDER)
    args = ap.parse_args()

    workdir = Path(args.dir)
    workdir.mkdir(parents=True, exist_ok=True)
    if args.mode == "send":
        with open(workdir / "cmd", "w") as f:
            f.write(" ".join(args.command) + "\n")
        return

    bridge = Bridge(args.port, workdir, args.width, args.height)
    bridge.connect()
    t = threading.Thread(target=bridge.reader, daemon=True)
    t.start()
    try:
        bridge.command_loop()
    except KeyboardInterrupt:
        pass
    bridge.running = False
    if bridge.ser:
        bridge.ser.close()


if __name__ == "__main__":
    sys.exit(main())
