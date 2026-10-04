#include "ReaderKeys.h"
#include "deckpoint/HelpKey.h"

namespace deckpoint::reader {

namespace {
bool isMarkLetter(const char c) { return c >= 'a' && c <= 'z'; }
}  // namespace

void ReaderKeys::reset() {
  count = 0;
  countDigits = 0;
  prefix = 0;
  text[0] = '\0';
}

void ReaderKeys::rebuildText() {
  char digits[4];
  int n = 0;
  uint16_t value = count;
  do {
    digits[n++] = static_cast<char>('0' + value % 10);
    value /= 10;
  } while (value > 0 && n < 3);
  int pos = 0;
  if (countDigits > 0) {
    while (n > 0) text[pos++] = digits[--n];
  }
  if (prefix != 0) text[pos++] = prefix;
  text[pos] = '\0';
}

ReaderCommand ReaderKeys::finish(const ReaderCmd type, const char arg) {
  ReaderCommand cmd;
  cmd.type = type;
  cmd.arg = arg;
  cmd.hasCount = countDigits > 0;
  cmd.count = cmd.hasCount ? count : 1;
  if (type == ReaderCmd::GoPercent && cmd.count > MAX_PERCENT) cmd.count = MAX_PERCENT;
  reset();
  return cmd;
}

ReaderCommand ReaderKeys::feed(const freeink::KeyEvent& event) {
  using freeink::SpecialKey;
  switch (event.special) {
    case SpecialKey::None:
      break;
    case SpecialKey::Escape:
    case SpecialKey::Backspace: {
      const bool hadPending = pending();
      reset();
      ReaderCommand cmd;
      cmd.type = hadPending ? ReaderCmd::Cancel : ReaderCmd::Back;
      return cmd;
    }
    case SpecialKey::Enter:
      return finish(ReaderCmd::Menu);
    case SpecialKey::PageDown:
    case SpecialKey::Down:
    case SpecialKey::Right:
      return finish(ReaderCmd::NextPage);
    case SpecialKey::PageUp:
    case SpecialKey::Up:
    case SpecialKey::Left:
      return finish(ReaderCmd::PrevPage);
    default:
      reset();
      return {};
  }

  const char c = event.ch;
  if (c == 0) {
    ReaderCommand cmd;
    cmd.type = pending() ? ReaderCmd::Pending : ReaderCmd::None;
    return cmd;
  }
  if (deckpoint::isHelpKey(event)) return finish(ReaderCmd::Help);

  if (prefix != 0) {
    switch (prefix) {
      case 'g':
        if (c == 'g') return finish(ReaderCmd::BookStart);
        break;
      case ']':
        if (c == ']') return finish(ReaderCmd::NextChapter);
        break;
      case '[':
        if (c == '[') return finish(ReaderCmd::PrevChapter);
        break;
      case 'm':
        if (isMarkLetter(c)) return finish(ReaderCmd::SetMark, c);
        break;
      case '\'':
        if (isMarkLetter(c)) return finish(ReaderCmd::JumpMark, c);
        if (c == '\'') return finish(ReaderCmd::JumpBack);
        break;
      default:
        break;
    }
    reset();
    return {};
  }

  // A leading 0 is not a count (there is no "0 pages").
  if ((c >= '1' && c <= '9') || (c == '0' && countDigits > 0)) {
    const uint32_t next = static_cast<uint32_t>(count) * 10 + static_cast<uint32_t>(c - '0');
    count = next > MAX_COUNT ? MAX_COUNT : static_cast<uint16_t>(next);
    if (countDigits < 3) countDigits++;
    rebuildText();
    ReaderCommand cmd;
    cmd.type = ReaderCmd::Pending;
    return cmd;
  }

  switch (c) {
    case 'j':
    case 'l':
    case ' ':
      return finish(ReaderCmd::NextPage);
    case 'k':
    case 'h':
    case 'b':
      return finish(ReaderCmd::PrevPage);
    case 'g':
    case '[':
    case ']':
    case 'm':
    case '\'': {
      prefix = c;
      rebuildText();
      ReaderCommand cmd;
      cmd.type = ReaderCmd::Pending;
      return cmd;
    }
    case 'G':
      return finish(countDigits > 0 ? ReaderCmd::GoPercent : ReaderCmd::BookEnd);
    case '%':
      if (countDigits > 0) return finish(ReaderCmd::GoPercent);
      break;
    case ')':
      return finish(ReaderCmd::NextChapter);
    case '(':
      return finish(ReaderCmd::PrevChapter);
    case 't':
      return finish(ReaderCmd::Toc);
    case 'B':
      return finish(ReaderCmd::ToggleBookmark);
    case 'd':
      return finish(ReaderCmd::Dictionary);
    case 'D':
      return finish(ReaderCmd::LookupWord);
    case '/':
      return finish(ReaderCmd::Search);
    case 'n':
      return finish(ReaderCmd::SearchNext);
    case 'N':
      return finish(ReaderCmd::SearchPrev);
    case ':':
      return finish(ReaderCmd::CommandLine);
    default:
      break;
  }
  reset();
  return {};
}

}  // namespace deckpoint::reader
