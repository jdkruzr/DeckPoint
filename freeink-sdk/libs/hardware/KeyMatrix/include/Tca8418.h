#pragma once

// FreeInk SDK — TI TCA8418 I2C keypad scanner (DECKPOINT).
//
// Chip-level driver only: configures the matrix, drains the 10-deep key event
// FIFO and clears the interrupt. Keymaps, modifiers and what a key *means*
// belong to the board (see BoardTDeckPro). Written from the TI datasheet
// (SCPS215); register names follow it.
//
// Event codes: matrix key at (row, col) reports code row*10 + col + 1, with
// bit 7 set on press and clear on release.

#include <Arduino.h>
#include <Wire.h>

namespace freeink {

class Tca8418 {
 public:
  struct RawEvent {
    uint8_t code;  // 1..80 (matrix keys)
    bool pressed;
  };

  // Configure a rows x cols matrix (rows <= 8, cols <= 10) with key-event
  // interrupts enabled. Returns false if the chip does not ACK.
  bool begin(TwoWire& wire, uint8_t address, uint8_t rows, uint8_t cols);

  // Pop one event from the FIFO. Returns false when the FIFO is empty or the
  // bus fails.
  bool readEvent(RawEvent& out);

  // Number of events waiting (KEY_LCK_EC[3:0]).
  uint8_t pendingCount();

  // Clear K_INT (and overflow) after draining, releasing the INT line.
  void clearInterrupts();

  // Discard everything in the FIFO.
  void flush();

  bool present() const { return _present; }

 private:
  bool writeReg(uint8_t reg, uint8_t value);
  bool readReg(uint8_t reg, uint8_t& value);

  TwoWire* _wire = nullptr;
  uint8_t _addr = 0;
  bool _present = false;
};

}  // namespace freeink
