#pragma once

// DECKPOINT: physical keyboard HAL. Wraps the board's keyboard driver (today
// the T-Deck Pro's TCA8418 via BoardTDeckPro) behind the shared KeyEvent
// model so reader code never names a board. On boards without a keyboard
// every call is a no-op and present() is false.
//
// Two modes:
//   * bridge (default): keys the board maps to buttons (Enter, Backspace,
//     h/j/k/l, ...) arrive as ordinary button presses through HalGPIO, so
//     every button-driven screen works unchanged;
//   * raw: the current activity consumes KeyEvents itself (ActivityManager
//     routes them to Activity::onKey) and the bridge goes quiet.

#include <KeyEvent.h>

class HalKeyboard {
 public:
  bool present() const;
  bool pop(freeink::KeyEvent& out);
  void flush();
  void setRawMode(bool raw);
  bool rawMode() const;
};

extern HalKeyboard halKeyboard;
