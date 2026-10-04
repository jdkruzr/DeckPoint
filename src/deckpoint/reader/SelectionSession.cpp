#include "SelectionSession.h"

#include "deckpoint/annotations/AnnotationGeometry.h"

namespace deckpoint::reader {

namespace {

// U+2003 EM SPACE: the synthetic first-line indent some layouts prepend.
bool hasIndentPrefix(const char* text, const size_t bytes) {
  return bytes >= 3 && static_cast<uint8_t>(text[0]) == 0xE2 && static_cast<uint8_t>(text[1]) == 0x80 &&
         static_cast<uint8_t>(text[2]) == 0x83;
}

}  // namespace

WordExtent wordExtent(const uint32_t visibleOffset, const char* text, const size_t bytes) {
  return {visibleOffset, visibleOffset + annotations::wordSourceLength(text, bytes)};
}

void SelectionSession::begin() {
  currentStage = Stage::PickStart;
  anchorWord = -1;
  headWord = -1;
}

void SelectionSession::beginAt(const int word) {
  if (word < 0) {
    begin();
    return;
  }
  currentStage = Stage::PickEnd;
  anchorWord = word;
  headWord = -1;
}

void SelectionSession::reset() {
  currentStage = Stage::Idle;
  anchorWord = -1;
  headWord = -1;
}

bool SelectionSession::pick(const int word) {
  if (word < 0) return false;
  switch (currentStage) {
    case Stage::PickStart:
      anchorWord = word;
      currentStage = Stage::PickEnd;
      return true;
    case Stage::PickEnd:
    case Stage::Range:
      headWord = word;
      currentStage = Stage::Range;
      return true;
    case Stage::Idle:
    default:
      return false;
  }
}

bool SelectionSession::pickSingle() {
  if (currentStage != Stage::PickEnd || anchorWord < 0) return false;
  headWord = anchorWord;
  currentStage = Stage::Range;
  return true;
}

bool SelectionSession::back() {
  switch (currentStage) {
    case Stage::Range:
      headWord = -1;
      currentStage = Stage::PickEnd;
      return true;
    case Stage::PickEnd:
      anchorWord = -1;
      currentStage = Stage::PickStart;
      return true;
    default:
      return false;
  }
}

int SelectionSession::first() const {
  if (anchorWord < 0) return -1;
  if (headWord < 0) return anchorWord;
  return anchorWord < headWord ? anchorWord : headWord;
}

int SelectionSession::last() const {
  if (anchorWord < 0) return -1;
  if (headWord < 0) return anchorWord;
  return anchorWord > headWord ? anchorWord : headWord;
}

OffsetRange SelectionSession::range(const WordExtent* words, const size_t count) const {
  const int a = first();
  const int b = last();
  if (a < 0 || static_cast<size_t>(b) >= count) return {};
  return span(words[a], words[b]);
}

OffsetRange SelectionSession::span(const WordExtent firstWord, const WordExtent lastWord) {
  // Reading order normally means first.start <= last.start; take the outer
  // bounds anyway so a mixed-direction line never yields an inverted range.
  OffsetRange r;
  r.start = firstWord.start < lastWord.start ? firstWord.start : lastWord.start;
  r.end = firstWord.end > lastWord.end ? firstWord.end : lastWord.end;
  return r;
}

SelectionText::SelectionText(const OffsetRange range, const size_t maxBytes, std::string& out)
    : range(range), maxBytes(maxBytes), out(out) {
  out.clear();
}

void SelectionText::add(const uint32_t visibleOffset, const char* text, size_t bytes) {
  if (full || bytes == 0) return;
  const WordExtent extent = wordExtent(visibleOffset, text, bytes);
  if (extent.end <= range.start || extent.start >= range.end) return;
  if (hasIndentPrefix(text, bytes)) {
    text += 3;
    bytes -= 3;
    if (bytes == 0) return;
  }
  const bool space = !out.empty() && extent.start > lastEnd;
  if (out.size() + (space ? 1 : 0) + bytes > maxBytes) {
    full = true;
    return;
  }
  if (space) out.push_back(' ');
  out.append(text, bytes);
  lastEnd = extent.end;
}

void ActionMenu::open(const Kind kind) {
  menuKind = kind;
  if (kind == Kind::Word) {
    items[0] = SelectionAction::LookUp;
    items[1] = SelectionAction::Highlight;
    items[2] = SelectionAction::Note;
  } else {
    items[0] = SelectionAction::EditNote;
    items[1] = SelectionAction::Delete;
    items[2] = SelectionAction::LookUp;
  }
  itemCount = 3;
  cursorRow = 0;
  chosenAction = SelectionAction::None;
}

char ActionMenu::shortcut(const SelectionAction action) {
  switch (action) {
    case SelectionAction::LookUp:
      return 'd';
    case SelectionAction::Highlight:
      return 'v';
    case SelectionAction::Note:
    case SelectionAction::EditNote:
      return 'n';
    case SelectionAction::Delete:
      return 'x';
    case SelectionAction::None:
    default:
      return 0;
  }
}

bool ActionMenu::choose(const int row) {
  if (row < 0 || row >= itemCount) return false;
  cursorRow = static_cast<uint8_t>(row);
  chosenAction = items[row];
  return true;
}

ActionMenu::Result ActionMenu::feed(const freeink::KeyEvent& event) {
  using freeink::SpecialKey;
  if (itemCount == 0) return Result::Ignored;
  switch (event.special) {
    case SpecialKey::None:
      break;
    case SpecialKey::Escape:
    case SpecialKey::Backspace:
      return Result::Cancelled;
    case SpecialKey::Enter:
      return choose(cursorRow) ? Result::Chosen : Result::Ignored;
    case SpecialKey::Up:
    case SpecialKey::Left:
      cursorRow = static_cast<uint8_t>((cursorRow + itemCount - 1) % itemCount);
      return Result::Moved;
    case SpecialKey::Down:
    case SpecialKey::Right:
      cursorRow = static_cast<uint8_t>((cursorRow + 1) % itemCount);
      return Result::Moved;
    default:
      return Result::Ignored;
  }
  char c = event.ch;
  if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  if (c == 'k') {
    cursorRow = static_cast<uint8_t>((cursorRow + itemCount - 1) % itemCount);
    return Result::Moved;
  }
  if (c == 'j') {
    cursorRow = static_cast<uint8_t>((cursorRow + 1) % itemCount);
    return Result::Moved;
  }
  for (uint8_t i = 0; i < itemCount; i++) {
    if (c != 0 && shortcut(items[i]) == c) return choose(i) ? Result::Chosen : Result::Ignored;
  }
  return Result::Ignored;
}

UiRect MenuLayout::row(const uint8_t index) const {
  return {box.x, static_cast<int16_t>(box.y + headerH + index * rowH), box.w, rowH};
}

int MenuLayout::hit(const int x, const int y) const {
  if (!box.contains(x, y)) return OUTSIDE;
  for (uint8_t i = 0; i < rows; i++) {
    if (row(i).contains(x, y)) return i;
  }
  return CHROME;
}

MenuLayout layoutMenu(const int screenW, const int screenH, const int bottomReserve, const int lineTop, const int lineH,
                      int w, const int headerH, const int rowH, const uint8_t rows) {
  if (w > screenW) w = screenW;
  if (w < 0) w = 0;
  const int h = headerH + rowH * rows;
  const int usableBottom = screenH - bottomReserve;
  int y;
  if (lineTop + lineH + h <= usableBottom) {
    y = lineTop + lineH;  // below the word's line
  } else if (lineTop - h >= 0) {
    y = lineTop - h;  // above it
  } else {
    y = usableBottom - h;
  }
  if (y < 0) y = 0;
  MenuLayout layout{};
  layout.box = {static_cast<int16_t>((screenW - w) / 2), static_cast<int16_t>(y), static_cast<int16_t>(w),
                static_cast<int16_t>(h)};
  layout.headerH = static_cast<int16_t>(headerH);
  layout.rowH = static_cast<int16_t>(rowH);
  layout.rows = rows;
  return layout;
}

int BarLayout::hit(const int x, const int y) const {
  if (cancel.contains(x, y)) return CANCEL;
  if (confirm.contains(x, y)) return CONFIRM;
  return NONE;
}

BarLayout layoutBar(const int screenW, const int screenH, const int bottomReserve, const int anchorTop,
                    const int rowH) {
  const int y = anchorTop >= screenH / 2 ? 0 : screenH - bottomReserve - rowH;
  const int half = screenW / 2;
  BarLayout bar{};
  bar.cancel = {0, static_cast<int16_t>(y), static_cast<int16_t>(half), static_cast<int16_t>(rowH)};
  bar.confirm = {static_cast<int16_t>(half), static_cast<int16_t>(y), static_cast<int16_t>(screenW - half),
                 static_cast<int16_t>(rowH)};
  return bar;
}

}  // namespace deckpoint::reader
