// DECKPOINT: Hynitron CST3530 / CST328 touch driver. See Cst3xxTouch.h.
// Command sequences adapted from SensorLib (MIT, Copyright (c) 2022-2026 lewis he).

#include "Cst3xxTouch.h"

#include <BoardConfig.h>

#if FREEINK_CAP_TOUCH

#include <Arduino.h>
#include <Wire.h>

namespace freeink {
namespace cst3xx {
namespace {

uint8_t s_addr = I2C_ADDRESS;
int8_t s_irqPin = -1;
Info s_info = {Chip::None, 0, 0, 0};
uint32_t s_checksumErrors = 0;
volatile bool s_irqFlag = false;

void IRAM_ATTR onTouchIrq() { s_irqFlag = true; }

// Hynitron's own driver ends the command write with a STOP before reading.
bool writeBytes(const uint8_t* data, uint8_t len) {
  Wire.beginTransmission(s_addr);
  Wire.write(data, len);
  return Wire.endTransmission(true) == 0;
}

bool readBytes(uint8_t* buf, uint8_t len) {
  const uint8_t got = Wire.requestFrom(s_addr, len, static_cast<uint8_t>(true));
  if (got != len) {
    while (Wire.available()) Wire.read();
    return false;
  }
  for (uint8_t i = 0; i < len; i++) buf[i] = static_cast<uint8_t>(Wire.read());
  return true;
}

bool command32(uint32_t cmd) {
  const uint8_t b[4] = {static_cast<uint8_t>(cmd >> 24), static_cast<uint8_t>(cmd >> 16),
                        static_cast<uint8_t>(cmd >> 8), static_cast<uint8_t>(cmd)};
  return writeBytes(b, sizeof(b));
}

bool read32(uint32_t cmd, uint8_t* buf, uint8_t len) { return command32(cmd) && readBytes(buf, len); }

bool command16(uint16_t reg) {
  const uint8_t b[2] = {static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg)};
  return writeBytes(b, sizeof(b));
}

bool read16(uint16_t reg, uint8_t* buf, uint8_t len) { return command16(reg) && readBytes(buf, len); }

void hardwareReset(int8_t pin) {
  if (pin < 0) return;
  pinMode(pin, OUTPUT);
  digitalWrite(pin, HIGH);
  delay(2);
  digitalWrite(pin, LOW);
  delay(20);
  digitalWrite(pin, HIGH);
}

bool probeCst3530() {
  uint8_t info[CST3530_INFO_LEN];
  for (int attempt = 0; attempt < 5; attempt++) {
    if (attempt) {
      command32(CST3530_CMD_WAKE_I2C);  // leave low-power I2C, then retry
      delay(20);
    }
    if (!read32(CST3530_CMD_INFO, info, sizeof(info))) continue;
    if (info[2] != 0xCA || info[3] != 0xCA) continue;
    s_info.chip = Chip::Cst3530;
    s_info.resolutionX = static_cast<uint16_t>(info[28] | (info[29] << 8));
    s_info.resolutionY = static_cast<uint16_t>(info[30] | (info[31] << 8));
    s_info.firmware =
        static_cast<uint32_t>(info[32]) | (static_cast<uint32_t>(info[33]) << 8) |
        (static_cast<uint32_t>(info[34]) << 16) | (static_cast<uint32_t>(info[35]) << 24);
    // Normal reporting mode (SensorLib TouchDrvCST3530::wakeup()).
    command32(CST3530_CMD_WAKE_I2C);
    delay(1);
    command32(CST3530_CMD_WAKE_I2C);
    command32(CST3530_CMD_NORMAL_0);
    command32(CST3530_CMD_NORMAL_1);
    command32(CST3530_CMD_NORMAL_2);
    return true;
  }
  return false;
}

bool probeCst328() {
  for (int attempt = 0; attempt < 3; attempt++) {
    if (attempt) delay(30);
    if (!command16(CST328_CMD_DEBUG_INFO)) continue;
    delay(10);
    uint8_t buf[4];
    if (!read16(CST328_REG_FW_CHECK, buf, sizeof(buf)) || buf[2] != 0xCA || buf[3] != 0xCA) continue;
    s_info.chip = Chip::Cst328;
    if (read16(CST328_REG_RESOLUTION, buf, sizeof(buf))) {
      s_info.resolutionX = static_cast<uint16_t>(buf[0] | (buf[1] << 8));
      s_info.resolutionY = static_cast<uint16_t>(buf[2] | (buf[3] << 8));
    }
    if (read16(CST328_REG_FW_VERSION, buf, sizeof(buf))) {
      s_info.firmware = static_cast<uint32_t>(buf[0]) | (static_cast<uint32_t>(buf[1]) << 8) |
                        (static_cast<uint32_t>(buf[2]) << 16) | (static_cast<uint32_t>(buf[3]) << 24);
    }
    command16(CST328_CMD_NORMAL);
    return true;
  }
  return false;
}

}  // namespace

Chip begin(const uint8_t address, const int8_t resetPin, const int8_t irqPin) {
  s_addr = address;
  s_info = {Chip::None, 0, 0, 0};
  const unsigned long resetAt = millis();
  hardwareReset(resetPin);
  delay(50);  // CST3530 ready after ~50 ms
  if (!probeCst3530()) {
    // CST328 needs up to 200 ms after reset (datasheet TRON).
    const unsigned long since = millis() - resetAt;
    if (since < 200) delay(200 - since);
    probeCst328();
  }
  if (s_info.chip == Chip::None) return Chip::None;

  s_irqPin = irqPin;
  s_irqFlag = false;
  if (irqPin >= 0) {
    pinMode(irqPin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(irqPin), onTouchIrq, FALLING);
    s_irqFlag = digitalRead(irqPin) == LOW;  // a report already waiting from before the attach
  }
  return s_info.chip;
}

const Info& info() { return s_info; }

const char* chipName(const Chip chip) {
  switch (chip) {
    case Chip::Cst3530:
      return "CST3530";
    case Chip::Cst328:
      return "CST328";
    default:
      return "none";
  }
}

// Edge-triggered only: re-reading inside one low pulse can return an already
// acknowledged (empty) frame, which would read as a lift.
bool reportPending() { return s_irqPin < 0 || s_irqFlag; }

bool read(Frame& frame) {
  s_irqFlag = false;
  frame.count = 0;
  if (s_info.chip == Chip::Cst3530) {
    uint8_t buf[CST3530_FRAME_LEN];
    const bool ok = read32(CST3530_CMD_READ, buf, sizeof(buf));
    command32(CST3530_CMD_CLEAR);  // acknowledge on every path
    if (!ok) return false;
    const DecodeResult r = decodeCst3530(buf, sizeof(buf), frame);
    if (r == DecodeResult::BadChecksum) s_checksumErrors++;
    return r == DecodeResult::Ok;
  }
  if (s_info.chip == Chip::Cst328) {
    uint8_t buf[CST328_FRAME_LEN];
    const bool ok = read16(CST328_REG_REPORT, buf, sizeof(buf));
    const uint8_t ack[3] = {static_cast<uint8_t>(CST328_REG_REPORT >> 8),
                            static_cast<uint8_t>(CST328_REG_REPORT & 0xFF), CST328_ACK_VALUE};
    writeBytes(ack, sizeof(ack));
    if (!ok) return false;
    return decodeCst328(buf, sizeof(buf), frame) == DecodeResult::Ok;
  }
  return false;
}

void sleep() {
  if (s_info.chip == Chip::Cst3530) {
    command32(CST3530_CMD_WAKE_I2C);
    delay(1);
    command32(CST3530_CMD_WAKE_I2C);
    command32(CST3530_CMD_SLEEP);
  } else if (s_info.chip == Chip::Cst328) {
    command16(CST328_CMD_DEEP_SLEEP);
  } else {
    return;
  }
  if (s_irqPin >= 0) detachInterrupt(digitalPinToInterrupt(s_irqPin));
}

uint32_t checksumErrors() { return s_checksumErrors; }

}  // namespace cst3xx
}  // namespace freeink

#else  // !FREEINK_CAP_TOUCH

namespace freeink {
namespace cst3xx {
namespace {
const Info kNoInfo = {Chip::None, 0, 0, 0};
}
Chip begin(uint8_t, int8_t, int8_t) { return Chip::None; }
const Info& info() { return kNoInfo; }
const char* chipName(Chip) { return "none"; }
bool reportPending() { return false; }
bool read(Frame& frame) {
  frame.count = 0;
  return false;
}
void sleep() {}
uint32_t checksumErrors() { return 0; }
}  // namespace cst3xx
}  // namespace freeink

#endif
