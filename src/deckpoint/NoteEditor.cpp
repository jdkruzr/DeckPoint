#include "NoteEditor.h"

#include <algorithm>
#include <cstring>
#include <new>

namespace deckpoint {

namespace {

// Line offsets are 16-bit.
constexpr size_t MAX_CAP = UINT16_MAX - 1;
// Typical note: a handful of lines; the vector grows past this only for long ones.
constexpr size_t LINE_RESERVE = 32;

bool isContinuation(const char c) { return (static_cast<uint8_t>(c) & 0xC0) == 0x80; }

}  // namespace

bool NoteEditor::open(std::string_view initial, const size_t capBytes) {
  close();
  size_t want = std::min(std::max(capBytes, initial.size()), MAX_CAP);
  if (initial.size() > want) {
    size_t n = want;
    while (n > 0 && isContinuation(initial[n])) n--;
    initial = initial.substr(0, n);
  }
  buf.reset(new (std::nothrow) char[want + 1]);
  if (!buf) return false;
  capacity = want;
  if (!initial.empty()) memcpy(buf.get(), initial.data(), initial.size());
  len = initial.size();
  buf[len] = '\0';
  cur = len;
  top = 0;
  editedFlag = false;
  lines.clear();
  lines.reserve(LINE_RESERVE);
  return true;
}

void NoteEditor::close() {
  buf.reset();
  len = 0;
  capacity = 0;
  cur = 0;
  top = 0;
  editedFlag = false;
  lines.clear();
  lines.shrink_to_fit();
}

size_t NoteEditor::prevBoundary(size_t pos) const {
  if (pos == 0) return 0;
  pos--;
  while (pos > 0 && isContinuation(buf[pos])) pos--;
  return pos;
}

size_t NoteEditor::nextBoundary(size_t pos) const {
  if (pos >= len) return len;
  pos++;
  while (pos < len && isContinuation(buf[pos])) pos++;
  return pos;
}

NoteEditor::Edit NoteEditor::insert(const char* utf8, const size_t bytes) {
  if (!buf || bytes == 0) return Edit::Unchanged;
  if (len + bytes > capacity) return Edit::Full;
  memmove(buf.get() + cur + bytes, buf.get() + cur, len - cur + 1);  // incl. NUL
  memcpy(buf.get() + cur, utf8, bytes);
  len += bytes;
  cur += bytes;
  editedFlag = true;
  return Edit::Changed;
}

NoteEditor::Edit NoteEditor::backspace() {
  if (!buf || cur == 0) return Edit::Unchanged;
  const size_t from = prevBoundary(cur);
  memmove(buf.get() + from, buf.get() + cur, len - cur + 1);
  len -= cur - from;
  cur = from;
  editedFlag = true;
  return Edit::Changed;
}

NoteEditor::Edit NoteEditor::deleteForward() {
  if (!buf || cur >= len) return Edit::Unchanged;
  const size_t to = nextBoundary(cur);
  memmove(buf.get() + cur, buf.get() + to, len - to + 1);
  len -= to - cur;
  editedFlag = true;
  return Edit::Changed;
}

NoteEditor::Edit NoteEditor::clear() {
  if (!buf || len == 0) return Edit::Unchanged;
  len = 0;
  cur = 0;
  top = 0;
  buf[0] = '\0';
  editedFlag = true;
  return Edit::Changed;
}

bool NoteEditor::left() {
  if (cur == 0) return false;
  cur = prevBoundary(cur);
  return true;
}

bool NoteEditor::right() {
  if (cur >= len) return false;
  cur = nextBoundary(cur);
  return true;
}

bool NoteEditor::home() {
  if (lines.empty()) return false;
  const size_t start = lines[cursorLine()].start;
  if (cur == start) return false;
  cur = start;
  return true;
}

bool NoteEditor::end() {
  if (lines.empty()) return false;
  const size_t e = lineCursorEnd(cursorLine());
  if (cur == e) return false;
  cur = e;
  return true;
}

void NoteEditor::pushLine(const size_t start, const size_t end, const bool newline) {
  lines.push_back({static_cast<uint16_t>(start), static_cast<uint16_t>(end), newline});
}

void NoteEditor::layout(const TextMeasure& measure, const int width) {
  lines.clear();
  const char* s = buf ? buf.get() : "";
  size_t lineStart = 0;
  int lineWidth = 0;
  size_t i = 0;
  while (i < len) {
    if (s[i] == '\n') {
      pushLine(lineStart, i, true);
      i++;
      lineStart = i;
      lineWidth = 0;
      continue;
    }
    size_t j = i;
    if (s[i] == ' ') {
      // Spaces hang at the end of the line: never a reason to wrap.
      while (j < len && s[j] == ' ') j++;
      lineWidth += measure(s + i, j - i);
      i = j;
      continue;
    }
    while (j < len && s[j] != ' ' && s[j] != '\n') j++;
    const int w = measure(s + i, j - i);
    if (lineWidth + w <= width) {
      lineWidth += w;
      i = j;
      continue;
    }
    if (i > lineStart) {
      // The word goes to a fresh line.
      pushLine(lineStart, i, false);
      lineStart = i;
      lineWidth = 0;
      if (w <= width) {
        lineWidth = w;
        i = j;
        continue;
      }
    }
    // Wider than a whole line: break it between code points (at least one per line).
    size_t k = i;
    while (k < j) {
      size_t fit = nextBoundary(k);
      while (fit < j) {
        const size_t next = nextBoundary(fit);
        if (measure(s + k, next - k) > width) break;
        fit = next;
      }
      if (fit < j) {
        pushLine(k, fit, false);
        k = fit;
      } else {
        lineStart = k;
        lineWidth = measure(s + k, j - k);
        k = j;
      }
    }
    i = j;
  }
  pushLine(lineStart, len, false);
  if (top >= lines.size()) top = lines.size() - 1;
}

size_t NoteEditor::cursorLine() const {
  if (lines.empty()) return 0;
  // Last line starting at or before the cursor: a cursor on a soft wrap shows
  // at the start of the next line, one before a '\n' at the end of its own.
  size_t lo = 0;
  size_t hi = lines.size() - 1;
  while (lo < hi) {
    const size_t mid = (lo + hi + 1) / 2;
    if (lines[mid].start <= cur) {
      lo = mid;
    } else {
      hi = mid - 1;
    }
  }
  return lo;
}

size_t NoteEditor::lineCursorEnd(const size_t i) const {
  const Line& l = lines[i];
  if (i + 1 >= lines.size() || l.newline) return l.end;
  // Soft wrap: the line's end is the next line's start.
  return l.end > l.start ? prevBoundary(l.end) : l.start;
}

int NoteEditor::cursorX(const TextMeasure& measure) const {
  if (lines.empty()) return 0;
  const Line& l = lines[cursorLine()];
  return measure(buf.get() + l.start, cur - l.start);
}

size_t NoteEditor::positionOnLine(const size_t i, const int x, const TextMeasure& measure) const {
  const Line& l = lines[i];
  const size_t last = lineCursorEnd(i);
  size_t best = l.start;
  int bestDist = x < 0 ? -x : x;
  for (size_t p = nextBoundary(l.start); p <= last && p > l.start; p = nextBoundary(p)) {
    const int d = measure(buf.get() + l.start, p - l.start) - x;
    const int dist = d < 0 ? -d : d;
    if (dist < bestDist) {
      best = p;
      bestDist = dist;
    }
    if (d > 0 || p >= last) break;
  }
  return best;
}

bool NoteEditor::up(const TextMeasure& measure) {
  const size_t line = cursorLine();
  if (lines.empty() || line == 0) return false;
  cur = positionOnLine(line - 1, cursorX(measure), measure);
  return true;
}

bool NoteEditor::down(const TextMeasure& measure) {
  const size_t line = cursorLine();
  if (line + 1 >= lines.size()) return false;
  cur = positionOnLine(line + 1, cursorX(measure), measure);
  return true;
}

void NoteEditor::moveTo(const size_t line, const int x, const TextMeasure& measure) {
  if (lines.empty()) return;
  cur = positionOnLine(std::min(line, lines.size() - 1), x, measure);
}

void NoteEditor::scrollToCursor(const size_t visibleLines) {
  if (visibleLines == 0 || lines.empty()) {
    top = 0;
    return;
  }
  const size_t line = cursorLine();
  if (line < top) top = line;
  if (line >= top + visibleLines) top = line - visibleLines + 1;
  const size_t maxTop = lines.size() > visibleLines ? lines.size() - visibleLines : 0;
  if (top > maxTop) top = maxTop;
}

SheetPlacement placeNoteSheet(const int screenH, const int bottomReserve, const int passageTop, const int passageBottom,
                              const int sheetH) {
  const int bottomY = std::max(0, screenH - bottomReserve - sheetH);
  const SheetPlacement bottom{static_cast<int16_t>(bottomY), static_cast<int16_t>(sheetH), false};
  const SheetPlacement topSheet{0, static_cast<int16_t>(sheetH), true};
  if (passageBottom <= bottomY) return bottom;
  if (passageTop >= sheetH) return topSheet;
  const int coveredBelow = passageBottom - std::max(bottomY, passageTop);
  const int coveredAbove = std::min(sheetH, passageBottom) - passageTop;
  return coveredAbove < coveredBelow ? topSheet : bottom;
}

}  // namespace deckpoint
