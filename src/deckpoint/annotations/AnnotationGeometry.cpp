#include "AnnotationGeometry.h"

#include <cstdio>

namespace deckpoint::annotations {

int spineFromXPointer(const std::string_view xpointer) {
  constexpr std::string_view FRAGMENT = "/DocFragment[";
  const size_t at = xpointer.find(FRAGMENT);
  if (at == std::string_view::npos) return -1;
  // Only the document root may precede it: "/body" or "/body[1]".
  const std::string_view root = xpointer.substr(0, at);
  if (root != "/body" && root != "/body[1]") return -1;
  size_t i = at + FRAGMENT.size();
  int n = 0;
  bool digits = false;
  while (i < xpointer.size() && xpointer[i] >= '0' && xpointer[i] <= '9') {
    n = n * 10 + (xpointer[i] - '0');
    if (n > 32767) return -1;
    digits = true;
    i++;
  }
  if (!digits || i >= xpointer.size() || xpointer[i] != ']' || n < 1) return -1;
  return n - 1;
}

uint32_t wordSourceLength(const char* text, const size_t bytes) {
  size_t i = 0;
  if (bytes >= 3 && static_cast<uint8_t>(text[0]) == 0xE2 && static_cast<uint8_t>(text[1]) == 0x80 &&
      static_cast<uint8_t>(text[2]) == 0x83) {
    i = 3;
  }
  uint32_t count = 0;
  for (; i < bytes; i++) {
    if ((static_cast<uint8_t>(text[i]) & 0xC0) != 0x80) count++;
  }
  return count > 0 ? count : 1;
}

void planPageMarks(const PageWord* words, const size_t wordCount, const HighlightRange* ranges, const size_t rangeCount,
                   const MarkStyle& style, const MeasureWord measure, void* ctx, std::vector<MarkRect>& out) {
  out.clear();
  if (wordCount == 0 || rangeCount == 0) return;

  // Per range: last highlighted word on the page and its right edge.
  std::vector<int32_t> lastWord(rangeCount, -1);
  std::vector<int16_t> lastRight(rangeCount, 0);

  bool spanActive = false;
  uint16_t spanLine = 0;
  int16_t spanTop = 0;
  int spanX0 = 0;
  int spanX1 = 0;
  const auto flush = [&]() {
    if (!spanActive) return;
    out.push_back({static_cast<int16_t>(spanX0), static_cast<int16_t>(spanTop + style.underlineOffset),
                   static_cast<int16_t>(spanX1 - spanX0), style.underlineThickness});
    spanActive = false;
  };

  for (size_t i = 0; i < wordCount; i++) {
    const PageWord& word = words[i];
    bool highlighted = false;
    for (size_t r = 0; r < rangeCount; r++) {
      if (rangesOverlap(word.start, word.end, ranges[r].start, ranges[r].end)) {
        highlighted = true;
        break;
      }
    }
    if (!highlighted) {
      flush();
      continue;
    }
    const int right = word.x + measure(ctx, i);
    for (size_t r = 0; r < rangeCount; r++) {
      if (rangesOverlap(word.start, word.end, ranges[r].start, ranges[r].end)) {
        lastWord[r] = static_cast<int32_t>(i);
        lastRight[r] = static_cast<int16_t>(right);
      }
    }
    if (spanActive && spanLine == word.line) {
      spanX1 = right;
    } else {
      flush();
      spanActive = true;
      spanLine = word.line;
      spanTop = word.lineTop;
      spanX0 = word.x;
      spanX1 = right;
    }
  }
  flush();

  for (size_t r = 0; r < rangeCount; r++) {
    if (!ranges[r].hasNote || lastWord[r] < 0) continue;
    const auto last = static_cast<size_t>(lastWord[r]);
    // Ends here when its last character is inside this word, or the page goes
    // on past it (trailing punctuation outside the range, a gap).
    const bool endsHere =
        ranges[r].end <= words[last].end || (last + 1 < wordCount && words[last + 1].start >= ranges[r].end);
    if (!endsHere) continue;
    out.push_back({static_cast<int16_t>(lastRight[r] + style.noteGap),
                   static_cast<int16_t>(words[last].lineTop + style.noteOffset), style.noteSize, style.noteSize});
  }
}

void formatTimestamp(const std::tm& local, char (&out)[20]) {
  // Unsigned and modulo-bounded so every field has a fixed width.
  const auto u = [](const int v, const unsigned mod) { return static_cast<unsigned>(v) % mod; };
  snprintf(out, sizeof(out), "%04u-%02u-%02u %02u:%02u:%02u", u(local.tm_year + 1900, 10000), u(local.tm_mon + 1, 100),
           u(local.tm_mday, 100), u(local.tm_hour, 100), u(local.tm_min, 100), u(local.tm_sec, 100));
}

bool parseTimestamp(const std::string_view text, int64_t& secondsOut) {
  if (text.size() != 19) return false;
  int v[6];
  constexpr size_t STARTS[6] = {0, 5, 8, 11, 14, 17};
  constexpr size_t WIDTHS[6] = {4, 2, 2, 2, 2, 2};
  constexpr char SEPS[5] = {'-', '-', ' ', ':', ':'};
  for (size_t f = 0; f < 6; f++) {
    v[f] = 0;
    for (size_t i = 0; i < WIDTHS[f]; i++) {
      const char c = text[STARTS[f] + i];
      if (c < '0' || c > '9') return false;
      v[f] = v[f] * 10 + (c - '0');
    }
    if (f < 5 && text[STARTS[f] + WIDTHS[f]] != SEPS[f]) return false;
  }
  if (v[0] < 1970 || v[1] < 1 || v[1] > 12 || v[2] < 1 || v[2] > 31 || v[3] > 23 || v[4] > 59 || v[5] > 60)
    return false;
  // Days since 1970-01-01 (Hinnant's days_from_civil).
  const int y = v[0] - (v[1] <= 2 ? 1 : 0);
  const auto m = static_cast<unsigned>(v[1]);
  const int era = y / 400;  // y >= 1969
  const auto yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + static_cast<unsigned>(v[2]) - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const int64_t days = static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
  secondsOut = days * 86400 + v[3] * 3600 + v[4] * 60 + v[5];
  return true;
}

}  // namespace deckpoint::annotations
