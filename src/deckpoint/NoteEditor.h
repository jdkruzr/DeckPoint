#pragma once

// DECKPOINT: multi-line note editing model (pure, host-tested). The reader's
// note sheet (reader/EpubReaderNote.cpp) draws it and feeds it keys.
//
// The text is UTF-8 in one fixed buffer allocated by open() (cap + 1 bytes,
// freed by close()), so typing never reallocates. The cursor is a byte offset
// on a code point boundary. layout() wraps the text into visual lines for a
// width, given a measure callback (px of a byte range): greedy word wrap,
// spaces hang at the end of a line, a word wider than the line is broken
// between code points, '\n' ends a line. Cursor up / down and scrolling work
// on the last layout, so call layout() after every edit.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace deckpoint {

// Advance in px of text[0, bytes) (not NUL-terminated).
struct TextMeasure {
  void* ctx;
  int (*fn)(void* ctx, const char* text, size_t bytes);
  int operator()(const char* text, const size_t bytes) const { return bytes == 0 ? 0 : fn(ctx, text, bytes); }
};

class NoteEditor {
 public:
  enum class Edit : uint8_t {
    Unchanged,  // nothing to do (e.g. Backspace at the start)
    Changed,
    Full,  // refused: the text would pass the cap
  };

  // One visual line: bytes [start, end) are drawn; `end` excludes a
  // terminating '\n' (newline set).
  struct Line {
    uint16_t start;
    uint16_t end;
    bool newline;
  };

  // Copies `initial` with the cursor at its end. The cap is
  // max(capBytes, initial.size()) so an imported longer note can still be
  // shortened. False on OOM (the editor stays closed).
  bool open(std::string_view initial, size_t capBytes);
  void close();
  bool isOpen() const { return buf != nullptr; }

  std::string_view text() const { return {buf.get() ? buf.get() : "", len}; }
  size_t size() const { return len; }
  size_t cap() const { return capacity; }
  size_t cursor() const { return cur; }
  // Some edit happened since open().
  bool edited() const { return editedFlag; }

  Edit insert(const char* utf8, size_t bytes);
  Edit insertChar(char c) { return insert(&c, 1); }
  Edit newline() { return insertChar('\n'); }
  Edit backspace();
  Edit deleteForward();
  Edit clear();

  // Cursor moves; false when the cursor did not move.
  bool left();
  bool right();
  bool home();  // start of the visual line
  bool end();   // end of the visual line
  bool up(const TextMeasure& measure);
  bool down(const TextMeasure& measure);
  // Puts the cursor nearest to x (px from the line's left) on visual line `line`.
  void moveTo(size_t line, int x, const TextMeasure& measure);

  void layout(const TextMeasure& measure, int width);
  size_t lineCount() const { return lines.size(); }
  const Line& line(const size_t i) const { return lines[i]; }
  size_t cursorLine() const;
  // Cursor x on its visual line (px from the line's left).
  int cursorX(const TextMeasure& measure) const;

  // First visible line; scrollToCursor keeps the cursor line inside a window
  // of `visibleLines` and never leaves blank lines below the last one.
  size_t scrollTop() const { return top; }
  void scrollToCursor(size_t visibleLines);

 private:
  size_t prevBoundary(size_t pos) const;
  size_t nextBoundary(size_t pos) const;
  // Last cursor position that still shows on visual line i.
  size_t lineCursorEnd(size_t i) const;
  size_t positionOnLine(size_t i, int x, const TextMeasure& measure) const;
  void pushLine(size_t start, size_t end, bool newline);

  std::unique_ptr<char[]> buf;
  size_t len = 0;
  size_t capacity = 0;
  size_t cur = 0;
  size_t top = 0;
  bool editedFlag = false;
  std::vector<Line> lines;
};

// Where the note sheet goes: below the passage when it fits there (just above
// the bottom band), else at the top of the screen; when the passage is so tall
// that both would cover some of it, the placement that covers less (ties: bottom).
struct SheetPlacement {
  int16_t y;
  int16_t h;
  bool atTop;
};

SheetPlacement placeNoteSheet(int screenH, int bottomReserve, int passageTop, int passageBottom, int sheetH);

}  // namespace deckpoint
