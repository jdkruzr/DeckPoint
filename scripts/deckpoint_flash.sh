#!/usr/bin/env bash
# DeckPoint dev loop: build + flash the tdeckpro env, pausing the serial bridge
# (scripts/deckpoint_serial.py) around the upload so esptool can use the port.
#   scripts/deckpoint_flash.sh [bridge-dir] [port]
# The bridge must be restarted afterwards (it is a separate long-running job).
set -euo pipefail
DIR="${1:-/tmp/deckpoint}"
PORT="${2:-/dev/ttyACM0}"
PIO="${PIO:-$HOME/.platformio/penv/bin/pio}"
cd "$(dirname "$0")/.."

"$PIO" run -e tdeckpro > "$DIR/build.log" 2>&1 || { grep -E "error|Error" "$DIR/build.log" | head -30; exit 1; }

if [ -p "$DIR/cmd" ] && pgrep -f "deckpoint_serial.py daemon" > /dev/null; then
  echo quit > "$DIR/cmd"
  for _ in $(seq 1 20); do pgrep -f "deckpoint_serial.py daemon" > /dev/null || break; sleep 0.25; done
fi

# No "-t nobuild": without a build pass the LDF never resolves libraries and
# patch_sdfat.py fails its "exactly one SdFat" check. The rebuild is a no-op.
"$PIO" run -e tdeckpro -t upload --upload-port "$PORT" > "$DIR/upload.log" 2>&1 \
  || { tail -20 "$DIR/upload.log"; exit 1; }
grep -E "RAM:|Flash:" "$DIR/build.log" | tail -2
echo "flashed OK"
