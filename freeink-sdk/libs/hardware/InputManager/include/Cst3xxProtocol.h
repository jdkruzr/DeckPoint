#pragma once

// DECKPOINT: Hynitron CST3530 / CST328 touch report protocol (pure, no I/O, host-testable).
//
// Register map, command words and report layouts adapted from SensorLib's
// TouchDrvCST3530 and TouchDrvCST226 (the latter covers the CST328), MIT
// License, Copyright (c) 2022-2026 lewis he, https://github.com/lewisxhe/SensorLib.
// The CST328 report address (0xD000) and its 0xD000AB acknowledge follow the
// CST328 datasheet V2.2 rather than SensorLib's single-byte register read.
//
// Both chips sit at I2C 0x1A and use big-endian multi-byte command words:
//   CST3530: 32-bit commands (0xD0xxxxxx), every read is "write command, then read".
//   CST328 : 16-bit registers (0xD000 report, 0xD1xx mode/info).

#include <cstddef>
#include <cstdint>

namespace freeink {
namespace cst3xx {

constexpr uint8_t I2C_ADDRESS = 0x1A;
constexpr uint8_t MAX_POINTS = 5;

// --- CST3530 -----------------------------------------------------------------
constexpr uint32_t CST3530_CMD_READ = 0xD0070000;   // report frame
constexpr uint32_t CST3530_CMD_INFO = 0xD0030000;   // 50-byte info block
constexpr uint32_t CST3530_CMD_CLEAR = 0xD00002AB;  // acknowledge the report (always, even on errors)
constexpr uint32_t CST3530_CMD_SLEEP = 0xD00022AB;  // deep sleep; wake needs a hardware reset
constexpr uint32_t CST3530_CMD_WAKE_I2C = 0xD0000400;  // leave low-power I2C (sent before mode commands)
constexpr uint32_t CST3530_CMD_NORMAL_0 = 0xD0000000;
constexpr uint32_t CST3530_CMD_NORMAL_1 = 0xD0000C00;
constexpr uint32_t CST3530_CMD_NORMAL_2 = 0xD0000100;
constexpr uint8_t CST3530_INFO_LEN = 50;
constexpr uint8_t CST3530_FRAME_LEN = 32;
constexpr uint8_t CST3530_REPORT_POSITION = 0xFF;  // frame[2]: 0xF0 gesture, 0xE0 proximity

// --- CST328 ------------------------------------------------------------------
constexpr uint16_t CST328_REG_REPORT = 0xD000;
constexpr uint16_t CST328_CMD_DEBUG_INFO = 0xD101;  // enter info mode (registers 0xD1F4..)
constexpr uint16_t CST328_CMD_NORMAL = 0xD109;      // back to normal reporting
constexpr uint16_t CST328_CMD_DEEP_SLEEP = 0xD105;  // wake needs a hardware reset
constexpr uint16_t CST328_REG_RESOLUTION = 0xD1F8;  // [1:0] X, [3:2] Y, little-endian
constexpr uint16_t CST328_REG_FW_CHECK = 0xD1FC;    // [3:2] == 0xCACA when firmware is valid
constexpr uint16_t CST328_REG_CHIP_TYPE = 0xD204;   // [3:2] IC type, [1:0] project id
constexpr uint16_t CST328_REG_FW_VERSION = 0xD208;  // 0xA5A5A5A5 = no firmware
constexpr uint8_t CST328_ACK_VALUE = 0xAB;          // written to 0xD000 after each frame
constexpr uint8_t CST328_FRAME_LEN = 27;            // 0xD000..0xD01A

struct Point {
  uint16_t x;
  uint16_t y;
  uint8_t id;
};

struct Frame {
  uint8_t count;  // valid points in `points` (0 = no contact / released)
  Point points[MAX_POINTS];
};

enum class DecodeResult : uint8_t {
  Ok,         // `frame` holds the contacts (count may be 0 = all lifted)
  NotPosition,  // a gesture/proximity/empty report — ignore it, keep state
  BadChecksum,
};

inline uint16_t cst3530Checksum(const uint8_t* data, size_t len) {
  uint16_t sum = 0x55;
  for (size_t i = 0; i < len; i++) sum = static_cast<uint16_t>(sum + data[i]);
  return sum;
}

// CST3530 report: [0..1] checksum (LE) over [4 .. 4 + (keys+points)*5), [2] report
// type, [3] keys<<4 | points, then 5-byte records from [4]: xL, yL, pressure,
// yH<<4 | xH, event<<4 | id. Key records come first. event 0 = lifted.
inline DecodeResult decodeCst3530(const uint8_t* buf, size_t len, Frame& out) {
  out.count = 0;
  if (len < 4 || buf[2] != CST3530_REPORT_POSITION) return DecodeResult::NotPosition;
  const uint8_t points = buf[3] & 0x0F;
  const uint8_t keys = buf[3] >> 4;
  if (points == 0) return DecodeResult::Ok;
  if (points > MAX_POINTS) return DecodeResult::NotPosition;
  const size_t payload = static_cast<size_t>(keys + points) * 5;
  if (4 + payload > len) return DecodeResult::NotPosition;
  const uint16_t expected = static_cast<uint16_t>(buf[0] | (buf[1] << 8));
  if (cst3530Checksum(buf + 4, payload) != expected) return DecodeResult::BadChecksum;
  for (uint8_t i = 0; i < points; i++) {
    const uint8_t* r = buf + 4 + (keys + i) * 5;
    const uint8_t event = r[4] >> 4;
    if (event == 0) continue;
    Point& p = out.points[out.count++];
    p.x = static_cast<uint16_t>(r[0] | ((r[3] & 0x0F) << 8));
    p.y = static_cast<uint16_t>(r[1] | ((r[3] & 0xF0) << 4));
    p.id = r[4] & 0x0F;
  }
  return DecodeResult::Ok;
}

// CST328 report (0xD000..): record 1 at [0..4], [5] key flag (bit 7) | count, [6]
// fixed 0xAB, records 2..5 at [7], [12], [17], [22]. Record: id<<4 | state
// (0x06 = pressed), x[11:4], y[11:4], x[3:0]<<4 | y[3:0], pressure.
inline DecodeResult decodeCst328(const uint8_t* buf, size_t len, Frame& out) {
  out.count = 0;
  if (len < 7 || buf[6] != CST328_ACK_VALUE || buf[0] == CST328_ACK_VALUE) return DecodeResult::NotPosition;
  const uint8_t points = buf[5] & 0x7F;
  if (points == 0) return DecodeResult::Ok;
  if (points > MAX_POINTS) return DecodeResult::NotPosition;
  for (uint8_t i = 0; i < points; i++) {
    const size_t idx = i == 0 ? 0 : 7 + (i - 1) * 5;
    if (idx + 5 > len) break;
    const uint8_t* r = buf + idx;
    if ((r[0] & 0x0F) != 0x06) continue;
    Point& p = out.points[out.count++];
    p.x = static_cast<uint16_t>((r[1] << 4) | (r[3] >> 4));
    p.y = static_cast<uint16_t>((r[2] << 4) | (r[3] & 0x0F));
    p.id = r[0] >> 4;
  }
  return DecodeResult::Ok;
}

}  // namespace cst3xx
}  // namespace freeink
