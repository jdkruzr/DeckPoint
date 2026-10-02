#!/bin/bash
# DECKPOINT: compact UI fonts for small, low-PPI 1-bit panels (T-Deck Pro,
# 240x320 @ ~129 PPI). Same Ubuntu font stack and coverage as the stock UI
# fonts in convert-builtin-fonts.sh, but sized in pixels (--ppem) and
# rasterized with FreeType's monochrome hinting (--mono) instead of
# thresholding an anti-aliased render, which bloats small text on 1-bit glass.
#
# Slots on DECKPOINT_COMPACT_UI builds (src/main.cpp):
#   UI_12_FONT_ID -> ubuntu_c15   UI_10_FONT_ID -> ubuntu_c12   SMALL_FONT_ID -> ubuntu_c11
#
# Needs freetype-py + fonttools (lib/EpdFont/scripts/requirements.txt).
set -e
cd "$(dirname "$0")"
PY="${PYTHON:-python}"

# Shared with convert-builtin-fonts.sh: keep in sync.
ARABIC_INTERVALS=(
  --additional-intervals 0x060C,0x060C --additional-intervals 0x061B,0x061B
  --additional-intervals 0x061F,0x061F --additional-intervals 0x0621,0x0621
  --additional-intervals 0x0640,0x0640 --additional-intervals 0x0654,0x0654
  --additional-intervals 0x0660,0x0669 --additional-intervals 0x06BA,0x06BA
  --additional-intervals 0x06D4,0x06D4 --additional-intervals 0x06D5,0x06D5
  --additional-intervals 0x06F0,0x06F9 --additional-intervals 0xFB56,0xFB59
  --additional-intervals 0xFB66,0xFB69 --additional-intervals 0xFB7A,0xFB7D
  --additional-intervals 0xFB88,0xFB95 --additional-intervals 0xFB9E,0xFB9F
  --additional-intervals 0xFBA6,0xFBB1 --additional-intervals 0xFBFC,0xFBFF
  --additional-intervals 0xFE80,0xFEFC
)

for ppem in 15 12 11; do
  for style in Regular Bold; do
    font_name="ubuntu_c${ppem}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    src=../builtinFonts/source
    output_path="../builtinFonts/${font_name}.h"
    "$PY" fontconvert.py "$font_name" 0 \
      "$src/Ubuntu/Ubuntu-${style}.ttf" "$src/NotoSansHebrew/NotoSansHebrew-${style}.ttf" \
      "$src/NotoSansArabic/NotoSansArabic-${style}.ttf" "$src/Ubuntu/Ubuntu-Vietnamese-${style}.ttf" \
      --additional-intervals 0x05D0,0x05EA "${ARABIC_INTERVALS[@]}" --ppem "$ppem" --mono > "$output_path"
    echo "Generated $output_path"
  done
done

# Reader fonts: same families/styles as the stock 12/14/16/18 "pt" (150 DPI)
# sets, sized in pixels for ~129 PPI glass and registered into the stock reader
# font slots (12->15px, 14->17px, 16->19px, 18->21px). These stay 2-bit
# (anti-aliasing-ready); the panel shows them B/W until it has gray LUTs.
READER_STYLES=(Regular Italic Bold BoldItalic)
for ppem in 15 17 19 21; do
  for family in NotoSerif NotoSans; do
    lower=$(echo $family | tr '[:upper:]' '[:lower:]')
    for style in "${READER_STYLES[@]}"; do
      font_name="${lower}_c${ppem}_$(echo $style | tr '[:upper:]' '[:lower:]')"
      output_path="../builtinFonts/${font_name}.h"
      "$PY" fontconvert.py "$font_name" 0 "../builtinFonts/source/${family}/${family}-${style}.ttf" \
        --2bit --compress --pnum --ppem "$ppem" > "$output_path"
      echo "Generated $output_path"
    done
  done
done
