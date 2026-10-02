#pragma once

// FreeInk SDK — shared key event model.
//
// DECKPOINT: hoisted out of BleKeyboardHost.h so every keyboard source (BLE HID
// host, the T-Deck Pro's TCA8418 matrix, a future USB host) hands the firmware
// the same struct. BleKeyboardHost.h now includes this header.

#include <stdint.h>

namespace freeink {

// Non-character keys an editor/UI cares about. Printable keys arrive as `ch`.
enum class SpecialKey : uint8_t {
  None = 0,
  Enter,
  Backspace,
  Tab,
  Escape,
  Delete,
  Left,
  Right,
  Up,
  Down,
  Home,
  End,
  PageUp,
  PageDown,
};

// Modifier bits for KeyEvent::mods. Values match the HID left-hand modifier
// bits so BLE HID reports pass through unchanged.
namespace KeyMod {
constexpr uint8_t Ctrl = 0x01;
constexpr uint8_t Shift = 0x02;
constexpr uint8_t Alt = 0x04;
constexpr uint8_t Gui = 0x08;
}  // namespace KeyMod

// One decoded key press (or auto-repeat). `pressed` is always true today —
// sources emit on the press edge; releases are tracked internally.
struct KeyEvent {
  char ch = 0;                          // printable ASCII, or 0 for a special key
  uint8_t keycode = 0;                  // source-specific raw code (HID usage id, or matrix code)
  uint8_t mods = 0;                     // KeyMod bitmask
  SpecialKey special = SpecialKey::None;
  bool pressed = true;
};

}  // namespace freeink
