#include <gtest/gtest.h>

#include <cstdint>

#include "lib/GfxRenderer/ToneCurve.h"

namespace {

using ToneCurve::Histogram;

Histogram emptyHistogram() {
  Histogram h;
  h.clear();
  return h;
}

// Uniform ramp over [lo, hi], `perLevel` samples per gray level.
Histogram ramp(int lo, int hi, int perLevel = 10) {
  Histogram h = emptyHistogram();
  for (int g = lo; g <= hi; g++) {
    for (int i = 0; i < perLevel; i++) h.add(static_cast<uint8_t>(g));
  }
  return h;
}

void expectMonotonic(const uint8_t* lut) {
  for (int i = 1; i < ToneCurve::LUT_SIZE; i++) {
    EXPECT_LE(lut[i - 1], lut[i]) << "at " << i;
  }
}

void expectIdentity(const uint8_t* lut) {
  for (int i = 0; i < ToneCurve::LUT_SIZE; i++) EXPECT_EQ(lut[i], i) << "at " << i;
}

}  // namespace

TEST(ToneCurve, FullRangeBalancedImageIsIdentity) {
  uint8_t lut[ToneCurve::LUT_SIZE];
  const auto r = ToneCurve::buildLut(ramp(0, 255), lut);
  EXPECT_TRUE(r.identity);
  expectIdentity(lut);
}

TEST(ToneCurve, TooFewSamplesIsIdentity) {
  Histogram h = emptyHistogram();
  for (int i = 0; i < 10; i++) h.add(30);
  uint8_t lut[ToneCurve::LUT_SIZE];
  EXPECT_TRUE(ToneCurve::buildLut(h, lut).identity);
  expectIdentity(lut);
}

TEST(ToneCurve, FlatImageIsNotBlownUp) {
  uint8_t lut[ToneCurve::LUT_SIZE];
  // Solid near-black page with a little noise.
  auto r = ToneCurve::buildLut(ramp(14, 22, 100), lut);
  EXPECT_TRUE(r.identity);
  expectIdentity(lut);
  // Solid mid-gray.
  r = ToneCurve::buildLut(ramp(120, 130, 100), lut);
  EXPECT_TRUE(r.identity);
  expectIdentity(lut);
}

TEST(ToneCurve, LowContrastMidImageIsStretchedWithCappedGain) {
  uint8_t lut[ToneCurve::LUT_SIZE];
  const auto r = ToneCurve::buildLut(ramp(96, 160), lut);
  EXPECT_FALSE(r.identity);
  expectMonotonic(lut);
  // Spread increases, but never beyond MAX_GAIN (plus gamma slop).
  const int spread = lut[160] - lut[96];
  EXPECT_GT(spread, 64 * 3 / 2);
  EXPECT_LE(spread, static_cast<int>(64 * ToneCurve::MAX_GAIN) + 4);
  // Capped gain must not slam the range into pure black/white.
  EXPECT_GT(lut[96], 0);
  EXPECT_LT(lut[160], 255);
}

TEST(ToneCurve, WideLowContrastImageStretchesToFullRange) {
  uint8_t lut[ToneCurve::LUT_SIZE];
  const auto r = ToneCurve::buildLut(ramp(60, 200), lut);
  EXPECT_FALSE(r.identity);
  expectMonotonic(lut);
  EXPECT_LE(lut[r.blackPoint], 4);
  EXPECT_GE(lut[r.whitePoint], 251);
}

TEST(ToneCurve, DarkImageGetsShadowsLifted) {
  // Mostly near-black background with some shadow detail and a thin bright title.
  Histogram h = emptyHistogram();
  for (int i = 0; i < 6000; i++) h.add(static_cast<uint8_t>(20 + (i % 12)));
  for (int i = 0; i < 2500; i++) h.add(static_cast<uint8_t>(40 + (i % 60)));
  for (int i = 0; i < 300; i++) h.add(static_cast<uint8_t>(200 + (i % 30)));
  uint8_t lut[ToneCurve::LUT_SIZE];
  const auto r = ToneCurve::buildLut(h, lut);
  EXPECT_FALSE(r.identity);
  expectMonotonic(lut);
  EXPECT_LT(r.gammaX100, 100);
  EXPECT_GE(r.gammaX100, static_cast<int>(ToneCurve::MIN_GAMMA * 100));
  // Shadow detail that the 64-threshold dither would crush now lands in the dark-gray level.
  EXPECT_GE(lut[60], 64);
  EXPECT_GT(lut[80], lut[60] + 15);
  // The background stays dark, the title stays white.
  EXPECT_LT(lut[20], 40);
  EXPECT_GE(lut[220], 250);
}

TEST(ToneCurve, BrightImageIsBarelyChanged) {
  // Full-range, highlight-heavy image.
  Histogram h = emptyHistogram();
  for (int g = 0; g <= 255; g++) {
    const int n = g < 128 ? 2 : 12;
    for (int i = 0; i < n; i++) h.add(static_cast<uint8_t>(g));
  }
  uint8_t lut[ToneCurve::LUT_SIZE];
  const auto r = ToneCurve::buildLut(h, lut);
  expectMonotonic(lut);
  EXPECT_GE(r.gammaX100, 100);
  EXPECT_LE(r.gammaX100, static_cast<int>(ToneCurve::MAX_GAMMA * 100 + 0.5f));
  EXPECT_EQ(lut[0], 0);
  EXPECT_EQ(lut[255], 255);
  for (int i = 0; i < ToneCurve::LUT_SIZE; i++) EXPECT_LE(i - lut[i], 30) << "at " << i;
}

TEST(ToneCurve, LutIsMonotonicAcrossManyDistributions) {
  uint8_t lut[ToneCurve::LUT_SIZE];
  for (int lo = 0; lo < 256; lo += 17) {
    for (int hi = lo; hi < 256; hi += 23) {
      ToneCurve::buildLut(ramp(lo, hi, 3), lut);
      expectMonotonic(lut);
    }
  }
}
