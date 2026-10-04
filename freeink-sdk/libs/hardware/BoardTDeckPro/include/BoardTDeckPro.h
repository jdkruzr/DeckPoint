#pragma once

// FreeInk board support — LilyGo T-Deck Pro (DECKPOINT).
//
// Owns everything about this board that BoardConfig's static profile cannot
// express:
//   * early bus hygiene: one SPI bus is shared by the panel, SD card and SX1262,
//     so every chip select is parked high before anything touches it;
//   * peripheral power: GPS and LTE modem rails off, keyboard backlight off;
//   * hardware revision detection (v1.0 vs v1.1, by probing the v1.1-only
//     DRV2605 at 0x5A) and patching the revision-specific pins into ACTIVE;
//   * the TCA8418 keyboard: keymap, Shift/Sym/Alt modifiers, a KeyEvent queue
//     for keyboard-aware UI, and a button bridge (InputManager::setButtonHook)
//     so every button-driven screen works from the keyboard unchanged.
//
// Call begin() first thing in setup(), right after BoardConfig::holdPowerRails()
// and before storage or display init. Everything runs on the caller's thread:
// the keyboard is serviced from InputManager::update() via the button hook, so
// no I2C ever happens in an ISR.

#include <Arduino.h>
#include <KeyEvent.h>

namespace BoardTDeckPro {

enum class Revision : uint8_t { Unknown, V1_0, V1_1 };

void begin();
Revision revision();
const char* revisionName();
bool loraAsleep();
// Print the board report (revision, keyboard, LoRa) to Serial.
void logStatus();

// --- keyboard ----------------------------------------------------------------
bool keyboardPresent();

// Pop the next decoded key press. Keys are queued whether or not anything
// drains them (oldest dropped on overflow), so a screen that switches to raw
// mode never sees stale presses from before — call flushKeys() on entry.
bool popKey(freeink::KeyEvent& out);
void flushKeys();

// Raw mode: the screen consumes KeyEvents itself (text entry, vim keys), so the
// button bridge stops synthesizing BACK/CONFIRM/arrows from keys. The BOOT
// button (power) is a real GPIO and is unaffected.
void setRawKeyMode(bool raw);
bool rawKeyMode();

// Sticky modifier state for a status-bar indicator: shift / sym / alt are true
// while one is latched (one-shot for the next key) or locked (until tapped
// again); a modifier merely held down for a chord is not reported. Alt never
// locks (altLocked stays false).
struct ModifierState {
  bool shift, shiftLocked;
  bool sym, symLocked;
  bool alt, altLocked;
};
ModifierState modifiers();

// Feed a matrix key event as if the TCA8418 had reported it (code 1..35).
// For development tooling: lets a host drive the UI over serial.
void injectKey(uint8_t code, bool pressed);

// millis() of the last decoded key press (any key, bridged or not). Firmware
// folds this into its inactivity timer: in raw mode typing produces no button
// edges, so InputManager alone would let the device sleep mid-sentence.
unsigned long lastKeyActivityMs();

// Keyboard backlight (GPIO42).
void setKeyboardBacklight(bool on);
bool keyboardBacklight();
void toggleKeyboardBacklight();

// Deep sleep: the TCA8418 stays powered and pulls KEY_INT (GPIO15, an RTC GPIO)
// low on any keypress, so firmware can OR this into its ext1 wake mask to wake
// on any key. prepareForSleep() drains the FIFO so INT is released before sleep,
// and puts the touch controller into deep sleep.
uint64_t keyboardWakeMask();
void prepareForSleep();

}  // namespace BoardTDeckPro
