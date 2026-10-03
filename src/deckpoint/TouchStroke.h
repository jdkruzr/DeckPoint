#pragma once

// DECKPOINT: one touch contact's down and latest points (pure, host-testable).
//
// track() takes every contact sample, independent of any log or draw
// throttle, so the end point is the controller's final report before the lift
// rather than the last sample that happened to be logged.

#include <cstdint>

namespace deckpoint {

struct TouchStroke {
  int16_t downX = 0;
  int16_t downY = 0;
  int16_t lastX = 0;
  int16_t lastY = 0;
  bool active = false;

  void begin(const int16_t x, const int16_t y) {
    downX = lastX = x;
    downY = lastY = y;
    active = true;
  }

  void track(const int16_t x, const int16_t y) {
    if (!active) return;
    lastX = x;
    lastY = y;
  }

  // Closes the stroke at the final reported point. Returns false when no
  // stroke was open (a lift whose touch-down was never seen).
  bool end(const int16_t x, const int16_t y) {
    if (!active) return false;
    track(x, y);
    active = false;
    return true;
  }

  int dx() const { return lastX - downX; }
  int dy() const { return lastY - downY; }
};

}  // namespace deckpoint
