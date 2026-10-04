#pragma once

// DECKPOINT: which word a touch point lands on (pure, host-testable). Used by
// the reader's long-press popup and touch selection over PageWords' boxes.
//
// A box spans [x, x + width) by [y, y + lineHeight) (y = line top). A point
// inside a box picks it; otherwise the nearest box within `slop` px on both
// axes (a fingertip is wider than a short word, and lands in word gaps and
// line gaps too). Nothing within reach: -1.

#include <cstddef>

namespace deckpoint::reader {

// How far outside a word box a fingertip still picks it (logical px). Below
// half the T-Deck's ~17 px line gap pitch, so a press between two lines goes
// to the nearer one rather than skipping both.
constexpr int WORD_TOUCH_SLOP_PX = 8;

// Box: any type with integral x, y and width members (WordBox).
template <typename Box>
int findWordAt(const Box* boxes, const size_t count, const int x, const int y, const int lineHeight, const int slop) {
  int best = -1;
  long bestDist = 0;
  for (size_t i = 0; i < count; i++) {
    const int left = boxes[i].x;
    const int right = left + boxes[i].width;
    const int top = boxes[i].y;
    const int bottom = top + lineHeight;
    const int dx = x < left ? left - x : (x >= right ? x - right + 1 : 0);
    const int dy = y < top ? top - y : (y >= bottom ? y - bottom + 1 : 0);
    if (dx > slop || dy > slop) continue;
    const long dist = static_cast<long>(dx) * dx + static_cast<long>(dy) * dy;
    if (dist == 0) return static_cast<int>(i);
    if (best < 0 || dist < bestDist) {
      best = static_cast<int>(i);
      bestDist = dist;
    }
  }
  return best;
}

}  // namespace deckpoint::reader
