#pragma once

// DECKPOINT: pure helpers for placing and drawing annotations (host-tested):
// XPointer -> spine index, visible-offset ranges, the per-page underline /
// note-marker plan, and KOReader's timestamp format.

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string_view>
#include <vector>

namespace deckpoint::annotations {

// Spine index named by an EPUB XPointer ("/body/DocFragment[N]/..." or the
// explicit "/body[1]/DocFragment[N]/..."; DocFragment is 1-based). -1 if none.
int spineFromXPointer(std::string_view xpointer);

// Half-open ranges [aStart, aEnd) and [bStart, bEnd) share a character.
constexpr bool rangesOverlap(const uint32_t aStart, const uint32_t aEnd, const uint32_t bStart, const uint32_t bEnd) {
  return aStart < bEnd && bStart < aEnd;
}

// Source codepoints a laid-out word covers, from its visible offset: its
// codepoint count, minus a leading synthetic indent em-space (U+2003). At
// least 1, so a word always occupies its own offset.
uint32_t wordSourceLength(const char* text, size_t bytes);

// One highlight on the current chapter, as visible offsets [start, end).
struct HighlightRange {
  uint32_t start;
  uint32_t end;
  bool hasNote;
};

// One word of the page in reading order: its source range, left edge and line.
struct PageWord {
  uint32_t start;
  uint32_t end;
  int16_t x;
  int16_t lineTop;
  uint16_t line;
};

struct MarkRect {
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
};

struct MarkStyle {
  int16_t underlineOffset;     // from the line top
  int16_t underlineThickness;  // px
  int16_t noteSize;            // square marker side, px
  int16_t noteOffset;          // marker top, from the line top
  int16_t noteGap;             // gap between the last word and the marker
};

// Width of words[index] in px. Called only for highlighted words.
using MeasureWord = int (*)(void* ctx, size_t index);

// Fills `out` (cleared first) with the rectangles to draw for `ranges` on a
// page: one underline per run of consecutive highlighted words on a line
// (spanning the gaps between them), and a note marker after the last word of a
// noted highlight when the highlight ends on this page.
void planPageMarks(const PageWord* words, size_t wordCount, const HighlightRange* ranges, size_t rangeCount,
                   const MarkStyle& style, MeasureWord measure, void* ctx, std::vector<MarkRect>& out);

// KOReader's os.date("%Y-%m-%d %H:%M:%S") for a broken-down local time.
void formatTimestamp(const std::tm& local, char (&out)[20]);

// Inverse of formatTimestamp: "YYYY-MM-DD HH:MM:SS" as seconds since
// 1970-01-01 00:00:00 of the same (unspecified) zone. False when malformed.
bool parseTimestamp(std::string_view text, int64_t& secondsOut);  // years before 1970 are rejected

// Written when the clock has never been set: sorts before every real time, so
// AnnotationSync's newer-wins merge prefers any dated copy of the same
// highlight. Sync (round 2b) re-dates such entries before uploading.
constexpr const char* UNSET_TIMESTAMP = "1970-01-01 00:00:00";

}  // namespace deckpoint::annotations
