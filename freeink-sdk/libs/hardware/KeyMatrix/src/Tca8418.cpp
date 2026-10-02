#include "Tca8418.h"

namespace freeink {
namespace {
constexpr uint8_t REG_CFG = 0x01;
constexpr uint8_t REG_INT_STAT = 0x02;
constexpr uint8_t REG_KEY_LCK_EC = 0x03;
constexpr uint8_t REG_KEY_EVENT_A = 0x04;
constexpr uint8_t REG_KP_GPIO1 = 0x1D;  // ROW0..7 keypad-mode select
constexpr uint8_t REG_KP_GPIO2 = 0x1E;  // COL0..7
constexpr uint8_t REG_KP_GPIO3 = 0x1F;  // COL8..9

// CFG: INT_CFG (re-pulse INT if events remain when cleared), OVR_FLOW_IEN,
// KE_IEN. Auto-increment stays off — every access here is a single register.
constexpr uint8_t CFG_INT_CFG = 0x10;
constexpr uint8_t CFG_OVR_FLOW_IEN = 0x08;
constexpr uint8_t CFG_KE_IEN = 0x01;

constexpr uint8_t INT_STAT_K_INT = 0x01;
constexpr uint8_t INT_STAT_OVR_FLOW = 0x08;
constexpr uint8_t EVENT_PRESS_BIT = 0x80;
}  // namespace

bool Tca8418::writeReg(uint8_t reg, uint8_t value) {
  _wire->beginTransmission(_addr);
  _wire->write(reg);
  _wire->write(value);
  return _wire->endTransmission() == 0;
}

bool Tca8418::readReg(uint8_t reg, uint8_t& value) {
  _wire->beginTransmission(_addr);
  _wire->write(reg);
  if (_wire->endTransmission(false) != 0) return false;
  if (_wire->requestFrom(_addr, static_cast<uint8_t>(1)) != 1) return false;
  value = static_cast<uint8_t>(_wire->read());
  return true;
}

bool Tca8418::begin(TwoWire& wire, uint8_t address, uint8_t rows, uint8_t cols) {
  _wire = &wire;
  _addr = address;
  _present = false;
  if (rows == 0 || rows > 8 || cols == 0 || cols > 10) return false;

  _wire->beginTransmission(_addr);
  if (_wire->endTransmission() != 0) return false;

  const uint8_t rowMask = static_cast<uint8_t>((1U << rows) - 1U);
  const uint16_t colMask = static_cast<uint16_t>((1U << cols) - 1U);
  if (!writeReg(REG_KP_GPIO1, rowMask)) return false;
  if (!writeReg(REG_KP_GPIO2, static_cast<uint8_t>(colMask & 0xFF))) return false;
  if (!writeReg(REG_KP_GPIO3, static_cast<uint8_t>(colMask >> 8))) return false;
  if (!writeReg(REG_CFG, CFG_INT_CFG | CFG_OVR_FLOW_IEN | CFG_KE_IEN)) return false;

  _present = true;
  flush();
  return true;
}

uint8_t Tca8418::pendingCount() {
  uint8_t ec = 0;
  if (!_present || !readReg(REG_KEY_LCK_EC, ec)) return 0;
  return ec & 0x0F;
}

bool Tca8418::readEvent(RawEvent& out) {
  if (!_present) return false;
  uint8_t raw = 0;
  if (!readReg(REG_KEY_EVENT_A, raw) || raw == 0) return false;  // 0 = FIFO empty
  out.code = raw & 0x7F;
  out.pressed = (raw & EVENT_PRESS_BIT) != 0;
  return true;
}

void Tca8418::clearInterrupts() {
  if (!_present) return;
  writeReg(REG_INT_STAT, INT_STAT_K_INT | INT_STAT_OVR_FLOW);  // write-1-to-clear
}

void Tca8418::flush() {
  RawEvent e;
  for (uint8_t i = 0; i < 16 && readEvent(e); i++) {
  }
  clearInterrupts();
}

}  // namespace freeink
