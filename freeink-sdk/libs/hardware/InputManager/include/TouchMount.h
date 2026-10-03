#pragma once

// DECKPOINT: touch-to-panel mounting math (pure, host-testable).
//
// A controller reports (rawX, rawY) in its own frame. applyTouchMount() turns
// that into the display's native framebuffer frame, the frame InputManager
// reports in: optional axis swap first, then range clamp/offset, then per-axis
// flip. Ranges describe the POST-swap axes, as in BoardConfig::TouchConfig.

#include <cstdint>

namespace freeink {

struct TouchMount {
  bool swapXY;
  bool flipX;
  bool flipY;
  uint16_t rawMinX, rawMaxX;  // post-swap
  uint16_t rawMinY, rawMaxY;
};

struct TouchXY {
  uint16_t x;
  uint16_t y;
};

constexpr uint16_t clampToRange(uint16_t v, uint16_t lo, uint16_t hi) {
  return static_cast<uint16_t>((v <= lo ? 0 : (v >= hi ? hi - lo : v - lo)));
}

// Output spans 0..(rawMaxX-rawMinX) x 0..(rawMaxY-rawMinY).
constexpr TouchXY applyTouchMount(uint16_t rawX, uint16_t rawY, const TouchMount& m) {
  const uint16_t sx = m.swapXY ? rawY : rawX;
  const uint16_t sy = m.swapXY ? rawX : rawY;
  uint16_t x = clampToRange(sx, m.rawMinX, m.rawMaxX);
  uint16_t y = clampToRange(sy, m.rawMinY, m.rawMaxY);
  if (m.flipX) x = static_cast<uint16_t>((m.rawMaxX - m.rawMinX) - x);
  if (m.flipY) y = static_cast<uint16_t>((m.rawMaxY - m.rawMinY) - y);
  return {x, y};
}

// Panel-native framebuffer point -> logical Portrait point, the same transform
// as GfxRenderer::tapToLogical() for Orientation::Portrait (landscape-native
// panels that Portrait rotates). panelHeight is the native frame's height.
constexpr TouchXY nativeToPortrait(uint16_t x, uint16_t y, uint16_t panelHeight) {
  return {static_cast<uint16_t>(panelHeight - 1 - y), x};
}

}  // namespace freeink
