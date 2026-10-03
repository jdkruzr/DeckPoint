#pragma once

// DECKPOINT: touch calibration screen (serial: CMD:TOUCHTEST).
//
// Draws a grid with TL/TR/BL/BR labels in the logical Portrait frame and a dot
// wherever the firmware thinks the finger is. Every controller report is logged
//   [TOUCH] raw=(x,y) mapped=(x,y) n=<contacts> ev=<down|move|up> fb=(x,y)
// where raw is the controller's own coordinate, mapped is logical Portrait
// (what the user sees) and fb is the panel-native framebuffer point. A lift
// after movement also logs
//   [TOUCH] swipe=<LEFT|RIGHT|UP|DOWN> dx=<px> dy=<px> sdk=<0|1>
// (sdk = the SDK's own flick classifier accepted it). Reads the ungated
// InputManager, so it works while DECKPOINT_TOUCH_UI keeps touch out of the UI.
// Any key (or Back) exits.

#include "activities/Activity.h"

#include <freertos/FreeRTOS.h>

namespace deckpoint {

class TouchTestActivity final : public Activity {
 public:
  TouchTestActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool wantsRawKeys() const override { return true; }
  void onKey(const freeink::KeyEvent& event) override;
  bool preventAutoSleep() override { return true; }

 private:
  struct Dot {
    int16_t x;
    int16_t y;
    bool strokeStart;
  };
  static constexpr int MAX_DOTS = 96;
  // Refresh cadence while a finger is down; a lift always refreshes.
  static constexpr unsigned long RENDER_INTERVAL_MS = 300;
  // Minimum spacing between logged/drawn move samples.
  static constexpr unsigned long MOVE_LOG_INTERVAL_MS = 25;

  void addDot(int16_t x, int16_t y, bool strokeStart);

  // Written by loop(), copied by render() under `dotsLock` (a short spinlock,
  // not RenderLock, so sampling never waits out a panel refresh).
  portMUX_TYPE dotsLock = portMUX_INITIALIZER_UNLOCKED;
  Dot dots[MAX_DOTS]{};
  int dotHead = 0;  // index of the oldest dot
  int dotCount = 0;
  char status[64]{};
  Dot renderDots[MAX_DOTS]{};  // render-task copy

  bool wasDown = false;
  bool firstRender = true;
  bool dirty = false;
  int16_t downX = 0, downY = 0;  // logical Portrait touch-down point
  int16_t lastX = -1, lastY = -1;
  unsigned long lastMoveLogMs = 0;
  unsigned long lastRenderRequestMs = 0;
};

// Push the touch test over the current activity (no-op if already open).
void openTouchTest(GfxRenderer& renderer, MappedInputManager& mappedInput);

}  // namespace deckpoint
