// DECKPOINT: CST3530 / CST328 report decoding and T-Deck Pro touch mounting.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

#include "Cst3xxProtocol.h"
#include "TouchMount.h"

using namespace freeink;
using namespace freeink::cst3xx;

namespace {

// CST3530 frame: optional key records first, then one record per point.
struct Cst3530Rec {
  uint16_t x, y;
  uint8_t id, event;
};

void buildCst3530(uint8_t* buf, uint8_t keys, const Cst3530Rec* recs, uint8_t points) {
  memset(buf, 0, CST3530_FRAME_LEN);
  buf[2] = CST3530_REPORT_POSITION;
  buf[3] = static_cast<uint8_t>((keys << 4) | points);
  for (uint8_t i = 0; i < points; i++) {
    uint8_t* r = buf + 4 + (keys + i) * 5;
    r[0] = recs[i].x & 0xFF;
    r[1] = recs[i].y & 0xFF;
    r[2] = 0x40;  // pressure
    r[3] = static_cast<uint8_t>(((recs[i].y >> 8) << 4) | (recs[i].x >> 8));
    r[4] = static_cast<uint8_t>((recs[i].event << 4) | recs[i].id);
  }
  const uint16_t sum = cst3530Checksum(buf + 4, static_cast<size_t>(keys + points) * 5);
  buf[0] = sum & 0xFF;
  buf[1] = sum >> 8;
}

// T-Deck Pro initial mount (BoardConfig TDECK_TOUCH_*): 240x320 controller frame
// into the 320x240 landscape framebuffer.
constexpr TouchMount TDECK = {true, false, true, 0, 319, 0, 239};

}  // namespace

TEST(Cst3530Decode, SinglePoint) {
  uint8_t buf[CST3530_FRAME_LEN];
  const Cst3530Rec rec = {0x123, 0x0AB, 3, 0x0E};
  buildCst3530(buf, 0, &rec, 1);
  Frame f{};
  ASSERT_EQ(decodeCst3530(buf, sizeof(buf), f), DecodeResult::Ok);
  ASSERT_EQ(f.count, 1);
  EXPECT_EQ(f.points[0].x, 0x123);
  EXPECT_EQ(f.points[0].y, 0x0AB);
  EXPECT_EQ(f.points[0].id, 3);
}

TEST(Cst3530Decode, KeyRecordsComeFirst) {
  uint8_t buf[CST3530_FRAME_LEN];
  const Cst3530Rec recs[2] = {{10, 300, 0, 1}, {200, 20, 1, 1}};
  buildCst3530(buf, 1, recs, 2);
  Frame f{};
  ASSERT_EQ(decodeCst3530(buf, sizeof(buf), f), DecodeResult::Ok);
  ASSERT_EQ(f.count, 2);
  EXPECT_EQ(f.points[0].x, 10);
  EXPECT_EQ(f.points[0].y, 300);
  EXPECT_EQ(f.points[1].x, 200);
  EXPECT_EQ(f.points[1].y, 20);
}

TEST(Cst3530Decode, LiftedPointsAreDropped) {
  uint8_t buf[CST3530_FRAME_LEN];
  const Cst3530Rec rec = {50, 60, 0, 0};  // event 0 = lifted
  buildCst3530(buf, 0, &rec, 1);
  Frame f{};
  ASSERT_EQ(decodeCst3530(buf, sizeof(buf), f), DecodeResult::Ok);
  EXPECT_EQ(f.count, 0);
}

TEST(Cst3530Decode, ZeroPointsIsRelease) {
  uint8_t buf[CST3530_FRAME_LEN];
  buildCst3530(buf, 0, nullptr, 0);
  Frame f{};
  EXPECT_EQ(decodeCst3530(buf, sizeof(buf), f), DecodeResult::Ok);
  EXPECT_EQ(f.count, 0);
}

TEST(Cst3530Decode, RejectsBadChecksumAndNonPositionReports) {
  uint8_t buf[CST3530_FRAME_LEN];
  const Cst3530Rec rec = {1, 2, 0, 1};
  buildCst3530(buf, 0, &rec, 1);
  buf[0] ^= 0x01;
  Frame f{};
  EXPECT_EQ(decodeCst3530(buf, sizeof(buf), f), DecodeResult::BadChecksum);
  buildCst3530(buf, 0, &rec, 1);
  buf[2] = 0xF0;  // gesture report
  EXPECT_EQ(decodeCst3530(buf, sizeof(buf), f), DecodeResult::NotPosition);
}

TEST(Cst328Decode, TwoPoints) {
  uint8_t buf[CST328_FRAME_LEN] = {};
  // Record 1 at [0], record 2 at [7]; x = b1<<4 | b3>>4, y = b2<<4 | b3&0x0F.
  buf[0] = 0x06;  // id 0, pressed
  buf[1] = 0x0E;  // x 0xE5 = 229
  buf[2] = 0x13;  // y 0x13A = 314
  buf[3] = 0x5A;
  buf[5] = 2;
  buf[6] = CST328_ACK_VALUE;
  buf[7] = 0x16;  // id 1, pressed
  buf[8] = 0x01;  // x 0x012 = 18
  buf[9] = 0x02;  // y 0x023 = 35
  buf[10] = 0x23;
  Frame f{};
  ASSERT_EQ(decodeCst328(buf, sizeof(buf), f), DecodeResult::Ok);
  ASSERT_EQ(f.count, 2);
  EXPECT_EQ(f.points[0].x, 229);
  EXPECT_EQ(f.points[0].y, 314);
  EXPECT_EQ(f.points[1].x, 18);
  EXPECT_EQ(f.points[1].y, 35);
  EXPECT_EQ(f.points[1].id, 1);
}

TEST(Cst328Decode, InvalidFramesAreIgnored) {
  uint8_t buf[CST328_FRAME_LEN] = {};
  buf[5] = 1;
  Frame f{};
  EXPECT_EQ(decodeCst328(buf, sizeof(buf), f), DecodeResult::NotPosition);  // no 0xAB marker
  buf[6] = CST328_ACK_VALUE;
  buf[0] = CST328_ACK_VALUE;  // already acknowledged
  EXPECT_EQ(decodeCst328(buf, sizeof(buf), f), DecodeResult::NotPosition);
  buf[0] = 0x00;
  buf[5] = 0;
  EXPECT_EQ(decodeCst328(buf, sizeof(buf), f), DecodeResult::Ok);
  EXPECT_EQ(f.count, 0);
}

TEST(TouchMount, IdentityAndFlips) {
  constexpr TouchMount id = {false, false, false, 0, 99, 0, 49};
  EXPECT_EQ(applyTouchMount(10, 20, id).x, 10);
  EXPECT_EQ(applyTouchMount(10, 20, id).y, 20);
  constexpr TouchMount flipped = {false, true, true, 0, 99, 0, 49};
  EXPECT_EQ(applyTouchMount(10, 20, flipped).x, 89);
  EXPECT_EQ(applyTouchMount(10, 20, flipped).y, 29);
}

TEST(TouchMount, ClampsAndOffsetsToRange) {
  constexpr TouchMount m = {false, false, false, 10, 110, 20, 70};
  EXPECT_EQ(applyTouchMount(5, 500, m).x, 0);
  EXPECT_EQ(applyTouchMount(5, 500, m).y, 50);
  EXPECT_EQ(applyTouchMount(60, 45, m).x, 50);
  EXPECT_EQ(applyTouchMount(60, 45, m).y, 25);
}

// The initial T-Deck guess: the touch frame equals the UC8253 RAM frame, where
// controller (cx, cy) displays framebuffer (x = cy, y = 239 - cx)
// (Uc8253Gdeq031Driver::writePlane). Then the framebuffer point lands on the
// pixel the finger covers, and logical Portrait equals the raw coordinates.
TEST(TouchMount, TDeckInitialMountMatchesPanelTransposition) {
  for (uint16_t cx = 0; cx < 240; cx += 7) {
    for (uint16_t cy = 0; cy < 320; cy += 11) {
      const TouchXY fb = applyTouchMount(cx, cy, TDECK);
      ASSERT_EQ(fb.x, cy);
      ASSERT_EQ(fb.y, 239 - cx);
      const TouchXY portrait = nativeToPortrait(fb.x, fb.y, 240);
      ASSERT_EQ(portrait.x, cx);
      ASSERT_EQ(portrait.y, cy);
    }
  }
}

TEST(TouchMount, TDeckCorners) {
  // Raw top-left / bottom-right of the 240x320 controller frame.
  EXPECT_EQ(applyTouchMount(0, 0, TDECK).x, 0);
  EXPECT_EQ(applyTouchMount(0, 0, TDECK).y, 239);
  EXPECT_EQ(applyTouchMount(239, 319, TDECK).x, 319);
  EXPECT_EQ(applyTouchMount(239, 319, TDECK).y, 0);
}
