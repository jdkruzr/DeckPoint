#pragma once

// DECKPOINT: the sticky-modifier badge ("CAPS", "SYM", "ALT", ...) shown in
// headers, the reader status bar and text editors while a keyboard modifier is
// latched or locked, so a forgotten Caps Lock (which mutes j/k navigation and
// turns Enter into a newline in the note editor) is visible.
//
// ModifierDebounce decides what is shown: new bits appear only after they
// have been stable for SETTLE_MS, so Sym+':' typed at speed never flashes the
// badge (two refreshes for nothing); bits going away disappear at once, so
// the render the consuming key triggers already shows the badge gone.

#include <cstddef>
#include <cstdint>

namespace deckpoint {

class ModifierDebounce {
 public:
  static constexpr unsigned long SETTLE_MS = 300;

  // Feed the live keymods:: bits. True when shown() changed (redraw).
  bool update(uint8_t live, unsigned long nowMs);
  uint8_t shown() const { return shown_; }

 private:
  uint8_t shown_ = 0;
  uint8_t pending_ = 0;
  unsigned long since_ = 0;
};

// Labels for SHIFT, SHIFT_LOCK, SYM, SYM_LOCK, ALT (keymods bit order).
struct ModifierNames {
  const char* names[5];
};

// Space-separated labels for `bits` into out (always NUL-terminated).
// Returns the length written; 0 when no bit is set.
size_t formatModifierBadge(uint8_t bits, const ModifierNames& names, char* out, size_t outSize);

// Firmware side (ModifierBadgeUi.cpp): the bits currently shown and their
// badge text, limited to `mask` (static buffer; "" when nothing is shown).
// Render task only reads; the main loop's ActivityManager owns the debounce.
uint8_t shownModifiers();
const char* modifierBadgeText(uint8_t mask = 0xFF);
// Main loop: returns true and the previous bits when the shown set changed.
bool pollModifierBadge(uint8_t live, unsigned long nowMs, uint8_t* before);

}  // namespace deckpoint
