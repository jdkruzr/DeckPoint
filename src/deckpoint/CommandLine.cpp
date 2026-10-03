#include "CommandLine.h"

#include <cstring>

namespace deckpoint {

void CommandLine::open(const char* prefill) {
  isOpen_ = true;
  setText(prefill);
}

void CommandLine::setText(const char* text) {
  len = 0;
  if (text) {
    while (text[len] && len < MAX_LEN) {
      buf[len] = text[len];
      ++len;
    }
  }
  buf[len] = '\0';
}

void CommandLine::remember() {
  const char* p = buf;
  while (*p == ' ') ++p;
  if (*p == '\0') return;
  memcpy(history, buf, len + 1);
}

void CommandLine::setHistory(const char* text) {
  size_t n = 0;
  if (text) {
    while (text[n] && n < MAX_LEN) {
      history[n] = text[n];
      ++n;
    }
  }
  history[n] = '\0';
}

CommandLine::Action CommandLine::feed(const freeink::KeyEvent& event) {
  using freeink::SpecialKey;
  const bool alt = (event.mods & freeink::KeyMod::Alt) != 0;

  switch (event.special) {
    case SpecialKey::Enter:
      return Action::Submit;
    case SpecialKey::Escape:
      return Action::Cancel;
    case SpecialKey::Backspace:
    case SpecialKey::Delete:  // Shift+Backspace on the T-Deck Pro; the cursor is always at the end
      if (alt) {
        if (len == 0) return Action::None;
        setText("");
        return Action::Edited;
      }
      if (len == 0) return Action::Cancel;
      // Drop one UTF-8 character (continuation bytes are 10xxxxxx).
      do {
        --len;
      } while (len > 0 && (static_cast<uint8_t>(buf[len]) & 0xC0) == 0x80);
      buf[len] = '\0';
      return Action::Edited;
    case SpecialKey::Tab:
      return Action::Complete;
    case SpecialKey::Up:
      if (history[0] == '\0' || strcmp(history, buf) == 0) return Action::None;
      setText(history);
      return Action::Edited;
    case SpecialKey::None:
      break;
    default:
      return Action::None;
  }

  if (alt) {
    if (event.ch == ' ') return Action::Complete;
    if (event.ch == 'k') {
      if (history[0] == '\0' || strcmp(history, buf) == 0) return Action::None;
      setText(history);
      return Action::Edited;
    }
    return Action::None;
  }
  if ((event.mods & (freeink::KeyMod::Ctrl | freeink::KeyMod::Gui)) != 0) return Action::None;
  if (static_cast<uint8_t>(event.ch) < 0x20 || event.ch == 0x7F) return Action::None;
  if (len >= MAX_LEN) return Action::None;
  buf[len++] = event.ch;
  buf[len] = '\0';
  return Action::Edited;
}

}  // namespace deckpoint
