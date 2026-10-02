#pragma once

// DECKPOINT: compact reader fonts (lib/EpdFont/scripts/convert-deckpoint-compact-fonts.sh).
// The stock reader fonts are "points at 150 DPI"; on ~129 PPI glass the
// default 14 came out ~29px per line. Included right after builtinFonts/all.h
// on DECKPOINT_COMPACT_UI builds, these #defines point every stock reader
// font symbol at a pixel-sized 2-bit cut, so the existing declarations, font
// IDs and size setting keep working (12->15px 14->17px 16->19px 18->21px)
// and the linker drops the unused stock data.

#include <builtinFonts/notoserif_c15_regular.h>
#include <builtinFonts/notoserif_c15_italic.h>
#include <builtinFonts/notoserif_c15_bold.h>
#include <builtinFonts/notoserif_c15_bolditalic.h>
#include <builtinFonts/notoserif_c17_regular.h>
#include <builtinFonts/notoserif_c17_italic.h>
#include <builtinFonts/notoserif_c17_bold.h>
#include <builtinFonts/notoserif_c17_bolditalic.h>
#include <builtinFonts/notoserif_c19_regular.h>
#include <builtinFonts/notoserif_c19_italic.h>
#include <builtinFonts/notoserif_c19_bold.h>
#include <builtinFonts/notoserif_c19_bolditalic.h>
#include <builtinFonts/notoserif_c21_regular.h>
#include <builtinFonts/notoserif_c21_italic.h>
#include <builtinFonts/notoserif_c21_bold.h>
#include <builtinFonts/notoserif_c21_bolditalic.h>
#include <builtinFonts/notosans_c15_regular.h>
#include <builtinFonts/notosans_c15_italic.h>
#include <builtinFonts/notosans_c15_bold.h>
#include <builtinFonts/notosans_c15_bolditalic.h>
#include <builtinFonts/notosans_c17_regular.h>
#include <builtinFonts/notosans_c17_italic.h>
#include <builtinFonts/notosans_c17_bold.h>
#include <builtinFonts/notosans_c17_bolditalic.h>
#include <builtinFonts/notosans_c19_regular.h>
#include <builtinFonts/notosans_c19_italic.h>
#include <builtinFonts/notosans_c19_bold.h>
#include <builtinFonts/notosans_c19_bolditalic.h>
#include <builtinFonts/notosans_c21_regular.h>
#include <builtinFonts/notosans_c21_italic.h>
#include <builtinFonts/notosans_c21_bold.h>
#include <builtinFonts/notosans_c21_bolditalic.h>

#define notoserif_12_regular notoserif_c15_regular
#define notoserif_12_italic notoserif_c15_italic
#define notoserif_12_bold notoserif_c15_bold
#define notoserif_12_bolditalic notoserif_c15_bolditalic
#define notoserif_14_regular notoserif_c17_regular
#define notoserif_14_italic notoserif_c17_italic
#define notoserif_14_bold notoserif_c17_bold
#define notoserif_14_bolditalic notoserif_c17_bolditalic
#define notoserif_16_regular notoserif_c19_regular
#define notoserif_16_italic notoserif_c19_italic
#define notoserif_16_bold notoserif_c19_bold
#define notoserif_16_bolditalic notoserif_c19_bolditalic
#define notoserif_18_regular notoserif_c21_regular
#define notoserif_18_italic notoserif_c21_italic
#define notoserif_18_bold notoserif_c21_bold
#define notoserif_18_bolditalic notoserif_c21_bolditalic
#define notosans_12_regular notosans_c15_regular
#define notosans_12_italic notosans_c15_italic
#define notosans_12_bold notosans_c15_bold
#define notosans_12_bolditalic notosans_c15_bolditalic
#define notosans_14_regular notosans_c17_regular
#define notosans_14_italic notosans_c17_italic
#define notosans_14_bold notosans_c17_bold
#define notosans_14_bolditalic notosans_c17_bolditalic
#define notosans_16_regular notosans_c19_regular
#define notosans_16_italic notosans_c19_italic
#define notosans_16_bold notosans_c19_bold
#define notosans_16_bolditalic notosans_c19_bolditalic
#define notosans_18_regular notosans_c21_regular
#define notosans_18_italic notosans_c21_italic
#define notosans_18_bold notosans_c21_bold
#define notosans_18_bolditalic notosans_c21_bolditalic
