#pragma once

// DECKPOINT: which global touch gestures a board answers (pure, host-testable).
//
// A small per-board mask instead of scattered #ifs. Touch-first boards keep
// every CrossPoint gesture (and none of DeckPoint's). The T-Deck Pro (keyboard
// + small glass) keeps only the reader's top-edge swipe down to the reader
// menu: Back, Home and the light panel are keys there, and a 3.1" screen has
// no edge to spare. It adds long-press-a-word lookup in the reader.

#include <cstdint>

namespace deckpoint {
namespace touch {

enum Gesture : uint8_t {
  GESTURE_READER_MENU_SWIPE = 1u << 0,  // top edge, swipe down -> reader menu
  GESTURE_EDGE_BACK = 1u << 1,          // left edge, swipe right -> Back
  GESTURE_EDGE_HOME = 1u << 2,          // bottom edge, swipe up -> Home (boards without a Home key)
  GESTURE_HEADER_BACK_TAP = 1u << 3,    // tap on the header back button -> Back
  GESTURE_STATUS_BAR_TAP = 1u << 4,     // tap on a tab screen's status bar -> light panel
  GESTURE_LIGHT_PANEL_SWIPE = 1u << 5,  // top edge, swipe down -> light panel (boards with a front light)
  GESTURE_WORD_LOOKUP = 1u << 6,        // reader: long-press a word -> dictionary (DeckPoint)
};

// CrossPoint's gestures (touch-first boards).
constexpr uint8_t GESTURES_ALL = GESTURE_READER_MENU_SWIPE | GESTURE_EDGE_BACK | GESTURE_EDGE_HOME |
                                 GESTURE_HEADER_BACK_TAP | GESTURE_STATUS_BAR_TAP | GESTURE_LIGHT_PANEL_SWIPE;
constexpr uint8_t GESTURES_KEYBOARD_HYBRID = GESTURE_READER_MENU_SWIPE | GESTURE_WORD_LOOKUP;

// keyboardHybrid: the board's touch supplements a physical keyboard (T-Deck Pro).
constexpr uint8_t gestureMaskFor(const bool keyboardHybrid) {
  return keyboardHybrid ? GESTURES_KEYBOARD_HYBRID : GESTURES_ALL;
}

constexpr bool gestureEnabled(const uint8_t mask, const Gesture gesture) { return (mask & gesture) != 0; }

}  // namespace touch
}  // namespace deckpoint
