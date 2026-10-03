#pragma once

// DECKPOINT: vim-style key parser for the EPUB reader. Pure state machine (no
// Arduino, renderer or I18n) so the host test suite can drive it; the reader
// feeds it raw KeyEvents and executes the commands it returns.

#include <KeyEvent.h>

#include <cstdint>

namespace deckpoint::reader {

enum class ReaderCmd : uint8_t {
  None,     // key ignored (any half-typed prefix was dropped)
  Pending,  // a count or prefix is waiting for more keys; see pendingText()
  Cancel,   // Esc / Backspace dropped a half-typed prefix
  NextPage,
  PrevPage,
  BookStart,    // gg
  BookEnd,      // G
  NextChapter,  // ]]  )
  PrevChapter,  // [[  (  (first to the chapter start when not on its first page)
  GoPercent,    // N%  NG   count = percent (0-100)
  Toc,          // t
  SetMark,      // m<a-z>   arg = letter
  JumpMark,     // '<a-z>   arg = letter
  JumpBack,     // ''
  ToggleBookmark,  // B
  Menu,            // Enter
  Back,            // Esc / Backspace with nothing pending
  Help,            // ?
  Dictionary,  // d   hint labels on the page's words
  LookupWord,  // D   `:dict ` prompt
  // Reserved for later steps; parsed now so the bindings are stable.
  Search,       // /
  SearchNext,   // n
  SearchPrev,   // N
  CommandLine,  // :
};

struct ReaderCommand {
  ReaderCmd type = ReaderCmd::None;
  uint16_t count = 1;     // repeat count (1 when none was typed)
  bool hasCount = false;  // a count was typed explicitly
  char arg = 0;           // mark letter for SetMark / JumpMark
};

class ReaderKeys {
 public:
  static constexpr uint16_t MAX_COUNT = 999;
  static constexpr uint16_t MAX_PERCENT = 100;

  ReaderCommand feed(const freeink::KeyEvent& event);
  void reset();
  bool pending() const { return countDigits > 0 || prefix != 0; }
  // Typed-so-far keys ("12", "g", "3]", "m", "'"); empty when nothing is pending.
  const char* pendingText() const { return text; }

 private:
  ReaderCommand finish(ReaderCmd type, char arg = 0);
  void rebuildText();

  uint16_t count = 0;
  uint8_t countDigits = 0;
  char prefix = 0;  // 'g', '[', ']', 'm', '\'' or 0
  char text[8] = {};
};

}  // namespace deckpoint::reader
