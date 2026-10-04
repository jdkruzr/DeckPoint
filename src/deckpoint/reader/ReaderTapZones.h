#pragma once

// DECKPOINT: reader tap zones (pure, host-testable), used by
// ReaderUtils::detectTouchPageTurn / isTouchMenuTap.
//
// Center third (both axes) = reader menu when the menu tap is on. Otherwise,
// with both directions tap-enabled, the left third turns back and the right
// two-thirds turn forward; Inverted Tap / RTL books mirror the split. A sole
// tap-enabled direction gets the whole page.

namespace deckpoint::reader {

constexpr bool inMenuTapZone(const int x, const int y, const int width, const int height) {
  return x >= width / 3 && x < width - width / 3 && y >= height / 3 && y < height - height / 3;
}

struct TapTurn {
  bool prev;
  bool next;
};

constexpr TapTurn tapTurnAt(const int x, const int y, const int width, const int height, const bool menuTap,
                            const bool nextTaps, const bool prevTaps, const bool inverted) {
  if (menuTap && inMenuTapZone(x, y, width, height)) return {false, false};
  const bool nextZone = inverted ? x < (width * 2) / 3 : x >= width / 3;
  return {prevTaps && (!nextTaps || !nextZone), nextTaps && (!prevTaps || nextZone)};
}

}  // namespace deckpoint::reader
