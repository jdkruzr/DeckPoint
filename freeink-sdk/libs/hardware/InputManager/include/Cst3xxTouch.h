#pragma once

// DECKPOINT: Hynitron CST3530 / CST328 capacitive touch driver (T-Deck Pro).
//
// One controller per board, so state is module-level: InputManager drives
// begin()/read() and the board's sleep path calls sleep() without needing the
// InputManager instance. The chip is probed (CST3530 first, then CST328), not
// inferred from the board revision — both have shipped on the same PCB.
// All I2C runs on the caller's thread; the IRQ handler only sets a flag.
// Protocol notes and attribution: Cst3xxProtocol.h.

#include <cstdint>

#include "Cst3xxProtocol.h"

namespace freeink {
namespace cst3xx {

enum class Chip : uint8_t { None, Cst3530, Cst328 };

struct Info {
  Chip chip;
  uint16_t resolutionX;  // controller frame, 0 = not reported
  uint16_t resolutionY;
  uint32_t firmware;
};

// Reset (when resetPin >= 0), probe, enter normal reporting, arm the active-low
// IRQ. Expects Wire to be running on the touch bus already. Returns Chip::None
// when nothing answered.
Chip begin(uint8_t address, int8_t resetPin, int8_t irqPin);
const Info& info();
const char* chipName(Chip chip);

// True when the controller signalled a new report (IRQ falling edge) since the
// last read(). Always true without an IRQ pin (polled).
bool reportPending();

// Fetch, decode and acknowledge one report. Returns false when the I2C read
// failed or the frame was not a position report; `frame` is valid only on true.
// count == 0 means every contact lifted.
bool read(Frame& frame);

// Deep sleep (CST3530 0xD00022AB / CST328 0xD105). Only a hardware reset wakes
// it, which begin() does on the next boot.
void sleep();

// Diagnostics.
uint32_t checksumErrors();

}  // namespace cst3xx
}  // namespace freeink
