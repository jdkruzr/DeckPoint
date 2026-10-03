#pragma once

// DECKPOINT: one-line `:` prompt editing state (pure, host-tested). The owner
// feeds raw KeyEvents and acts on the returned Action; drawing and command
// execution live with the owner. Keys are the T-Deck Pro keymap's (no Tab or
// arrows there); Tab / Up are extra bindings for BLE keyboards.

#include <KeyEvent.h>

#include <cstddef>
#include <cstdint>

namespace deckpoint {

class CommandLine {
 public:
  static constexpr size_t MAX_LEN = 63;

  enum class Action : uint8_t {
    None,      // key ignored (line unchanged)
    Edited,    // text changed: repaint
    Submit,    // Enter
    Cancel,    // Esc, or Backspace on an empty line
    Complete,  // Alt+Space / Tab: caller completes the command name
  };

  void open(const char* prefill = "");
  void close() { isOpen_ = false; }
  bool isOpen() const { return isOpen_; }

  Action feed(const freeink::KeyEvent& event);

  const char* text() const { return buf; }
  size_t length() const { return len; }
  void setText(const char* text);
  // One-entry history: remember() stores the current text (when not blank),
  // Alt+k / Up brings it back.
  void remember();
  const char* lastCommand() const { return history; }
  void setHistory(const char* text);

 private:
  char buf[MAX_LEN + 1] = {};
  size_t len = 0;
  char history[MAX_LEN + 1] = {};
  bool isOpen_ = false;
};

}  // namespace deckpoint
