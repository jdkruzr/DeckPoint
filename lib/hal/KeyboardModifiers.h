#pragma once

#include <cstdint>

// DECKPOINT: sticky keyboard modifiers as a bit set (HalKeyboard::stickyModifiers()).
// Set while a modifier is latched (one-shot for the next key) or locked;
// a modifier only held down for a chord is not included.
namespace keymods {
constexpr uint8_t SHIFT = 1 << 0;
constexpr uint8_t SHIFT_LOCK = 1 << 1;  // caps lock
constexpr uint8_t SYM = 1 << 2;
constexpr uint8_t SYM_LOCK = 1 << 3;
constexpr uint8_t ALT = 1 << 4;  // Alt never locks
constexpr uint8_t LOCKS = SHIFT_LOCK | SYM_LOCK;
}  // namespace keymods
