#pragma once

// DECKPOINT: highlight selection on one reader page (pure, host-tested).
//
// SelectionSession tracks an anchor word and a head word (indices into the
// page's WordBox list) through the pick stages; range() turns them into the
// highlight's visible-offset range [start, end): from the first word's start to
// the last word's end, whichever order they were picked in. A word's end is its
// own token extent (wordExtent), so glued punctuation stays part of the token.
//
// SelectionText joins the page tokens a range covers into the highlight text
// (KOReader's `text` field). ActionMenu is the word / highlight popup's key
// handling, MenuLayout / BarLayout its geometry and touch hit-testing.

#include <KeyEvent.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace deckpoint::reader {

// A word's source extent, as visible offsets [start, end).
struct WordExtent {
  uint32_t start;
  uint32_t end;
};

struct OffsetRange {
  uint32_t start = 0;
  uint32_t end = 0;
  bool empty() const { return end <= start; }
  bool contains(const uint32_t offset) const { return offset >= start && offset < end; }
};

// Extent of a laid-out token from its visible offset and UTF-8 text.
WordExtent wordExtent(uint32_t visibleOffset, const char* text, size_t bytes);

class SelectionSession {
 public:
  enum class Stage : uint8_t {
    Idle,
    PickStart,  // waiting for the anchor
    PickEnd,    // anchor set, waiting for the head
    Range,      // both set: the range is shown
  };

  // Keyboard: start by picking the anchor.
  void begin();
  // Touch: the long-pressed word is the anchor already.
  void beginAt(int anchorWord);
  void reset();

  // PickStart: sets the anchor (-> PickEnd). PickEnd / Range: sets the head
  // (-> Range). False (unchanged) for a negative index or when Idle.
  bool pick(int word);
  // PickEnd: one-word selection (head = anchor, -> Range).
  bool pickSingle();
  // One stage back: Range -> PickEnd, PickEnd -> PickStart. False when there
  // is nothing to undo (PickStart / Idle).
  bool back();

  Stage stage() const { return currentStage; }
  int anchor() const { return anchorWord; }
  int head() const { return headWord; }
  // Lower / higher of anchor and head (the anchor alone before a head is set);
  // -1 without an anchor.
  int first() const;
  int last() const;
  bool covers(int word) const { return word >= 0 && first() >= 0 && word >= first() && word <= last(); }

  // [start of the first word, end of the last word); empty when nothing is
  // selected or an index is out of range.
  OffsetRange range(const WordExtent* words, size_t count) const;
  // The range spanned by the first and last selected words' extents.
  static OffsetRange span(WordExtent firstWord, WordExtent lastWord);

 private:
  Stage currentStage = Stage::Idle;
  int anchorWord = -1;
  int headWord = -1;
};

// Builds the highlight text from the page's tokens in reading order: tokens
// inside the range are joined, with one space where the source has whitespace
// between them (a gap in the visible offsets) and none where they touch
// ("word," stays glued). A leading synthetic indent (U+2003) is dropped. Stops
// before the text would pass maxBytes (whole tokens only).
class SelectionText {
 public:
  SelectionText(OffsetRange range, size_t maxBytes, std::string& out);
  void add(uint32_t visibleOffset, const char* text, size_t bytes);
  bool truncated() const { return full; }

 private:
  OffsetRange range;
  size_t maxBytes;
  std::string& out;
  uint32_t lastEnd = 0;
  bool full = false;
};

// An existing live highlight placed in the current chapter: its visible
// offsets and its AnnotationList index.
struct PlacedHighlight {
  uint32_t start;
  uint32_t end;
  int index;
};

// Touching or overlapping, ends inclusive: [0,5) and [5,9) touch, as in
// annotation_sweep.positions_intersect (which the sync merge collapses).
constexpr bool rangesTouch(const OffsetRange a, const OffsetRange b) { return a.start <= b.end && b.start <= a.end; }

// What a new selection does to the highlights it touches.
struct ExtendPlan {
  static constexpr uint8_t MAX_JOINED = 8;
  enum class Kind : uint8_t {
    None,     // touches nothing: a new highlight
    Extend,   // union with `count` highlights (indices, in position order)
    Covered,  // inside one highlight already: nothing to extend
    OffPage,  // a touched highlight leaves the page: not offered (page-local)
    TooMany,  // more than MAX_JOINED touched: not offered
  };
  Kind kind = Kind::None;
  OffsetRange range;  // the union (Extend / Covered)
  uint8_t count = 0;
  int indices[MAX_JOINED] = {};
};

// Joins every highlight that touches the selection, and transitively those
// touching the growing union. `page` bounds the page's words.
ExtendPlan planExtend(OffsetRange selection, const PlacedHighlight* highlights, size_t count, OffsetRange page);

enum class SelectionAction : uint8_t { None, LookUp, Highlight, Note, EditNote, Delete, Extend, Cancel };

// The popup over a word: Look up / Highlight / Note, over an existing
// highlight: Edit note / Delete / Look up, or after a selection touching
// existing highlights: Extend highlight / Cancel. j / k (and the arrows) move the
// cursor, Enter chooses it, a row's letter chooses that row, Esc / Backspace
// cancel.
class ActionMenu {
 public:
  enum class Kind : uint8_t { Word, Highlight, Extend };
  enum class Result : uint8_t { Ignored, Moved, Chosen, Cancelled };
  static constexpr uint8_t MAX_ITEMS = 3;

  void open(Kind kind);
  Kind kind() const { return menuKind; }
  uint8_t count() const { return itemCount; }
  SelectionAction item(uint8_t row) const { return row < itemCount ? items[row] : SelectionAction::None; }
  uint8_t cursor() const { return cursorRow; }
  SelectionAction chosen() const { return chosenAction; }

  Result feed(const freeink::KeyEvent& event);
  // A tapped row; false when out of range.
  bool choose(int row);

  // The row's key letter (shown on the row).
  static char shortcut(SelectionAction action);

 private:
  Kind menuKind = Kind::Word;
  SelectionAction items[MAX_ITEMS] = {};
  uint8_t itemCount = 0;
  uint8_t cursorRow = 0;
  SelectionAction chosenAction = SelectionAction::None;
};

struct UiRect {
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
  bool contains(const int px, const int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

// A popup box: an optional header (note preview) over `rows` contiguous rows,
// so each row's tap target is its full pitch.
struct MenuLayout {
  static constexpr int OUTSIDE = -1;
  static constexpr int CHROME = -2;

  UiRect box;
  int16_t headerH;
  int16_t rowH;
  uint8_t rows;

  UiRect row(uint8_t index) const;
  // Row index under the point; CHROME inside the box elsewhere, OUTSIDE beyond it.
  int hit(int x, int y) const;
};

// Places the box (width w, centered horizontally) just below the word's line
// when it fits above `bottomReserve`, else just above the line, else pinned to
// the bottom of the usable area.
MenuLayout layoutMenu(int screenW, int screenH, int bottomReserve, int lineTop, int lineH, int w, int headerH, int rowH,
                      uint8_t rows);

// Two full-width-half buttons (cancel | confirm) for touch picking. The bar
// goes to the half of the screen away from the anchor's line so the picked
// words stay visible: at the top when the anchor sits in the lower half.
struct BarLayout {
  static constexpr int NONE = -1;
  static constexpr int CANCEL = 0;
  static constexpr int CONFIRM = 1;

  UiRect cancel;
  UiRect confirm;
  int hit(int x, int y) const;
};

BarLayout layoutBar(int screenW, int screenH, int bottomReserve, int anchorTop, int rowH);

}  // namespace deckpoint::reader
