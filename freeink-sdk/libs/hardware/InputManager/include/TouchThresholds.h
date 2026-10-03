#pragma once

// DECKPOINT: single-contact touch classifier thresholds (pure, host-testable).
//
// Distances are in InputManager's mapped panel-native px. A board's
// TouchConfig may override each one (0 = SDK default); resolveTouchThresholds()
// fills the defaults and keeps the set consistent:
//   tapSlop <= tapReleaseSlop < swipeMin
// so a released contact is never both a tap and a swipe, and a contact that
// still counts as a tap on release has not been cancelled as a hold first.

#include <cstdint>

namespace freeink {

struct TouchThresholds {
  int swipeMinPx;        // travel that makes a lift a swipe
  int tapSlopPx;         // stationary slop: beyond it no hold/long-press
  int tapReleaseSlopPx;  // beyond it the lift is no longer a tap
};

constexpr int DEFAULT_TOUCH_SWIPE_MIN_PX = 60;
constexpr int DEFAULT_TOUCH_TAP_SLOP_PX = 28;

constexpr TouchThresholds DEFAULT_TOUCH_THRESHOLDS = {DEFAULT_TOUCH_SWIPE_MIN_PX, DEFAULT_TOUCH_TAP_SLOP_PX,
                                                      DEFAULT_TOUCH_SWIPE_MIN_PX - 1};

constexpr TouchThresholds resolveTouchThresholds(const uint8_t swipeMinPx, const uint8_t tapSlopPx,
                                                 const uint8_t tapReleaseSlopPx) {
  const int swipe = swipeMinPx >= 2 ? swipeMinPx : DEFAULT_TOUCH_SWIPE_MIN_PX;
  int release = tapReleaseSlopPx != 0 ? tapReleaseSlopPx : swipe - 1;
  if (release > swipe - 1) release = swipe - 1;
  int tap = tapSlopPx != 0 ? tapSlopPx : DEFAULT_TOUCH_TAP_SLOP_PX;
  if (tap > release) tap = release;
  return {swipe, tap, release};
}

}  // namespace freeink
