#pragma once

#include <cstdint>

// DECKPOINT: per-image auto-levels applied to 8-bit gray before 2-bit dithering.
// A cheap prepass fills a coarse luminance histogram; buildLut() turns it into a
// black/white-point stretch plus a mean-targeting gamma. Pure integer/float math
// with no Arduino dependencies so it runs in the host test suite.
namespace ToneCurve {

// Compile-time kill switch for every converter that runs the prepass.
constexpr bool ENABLED = true;

constexpr int HISTOGRAM_BINS = 64;
constexpr int BIN_SHIFT = 2;  // 256 gray levels / 64 bins
constexpr int LUT_SIZE = 256;

// Fewer samples than this is too noisy to judge; leave the image alone.
constexpr uint32_t MIN_SAMPLES = 64;
// Black/white points in per-mille of samples (1.5th and 99th percentile).
constexpr uint32_t BLACK_PERMILLE = 15;
constexpr uint32_t WHITE_PERMILLE = 990;
// Points already within this distance of 0/255 count as full range: no stretch.
constexpr int FULL_RANGE_SLACK = 10;
// Narrower than this is a near-flat image (solid fill, blank page): identity.
constexpr int MIN_RANGE = 24;
// Stretch gain ceiling, so low-contrast images do not amplify noise/JPEG blocking.
constexpr float MAX_GAIN = 2.5f;
// Gamma pulls the stretched mean toward this (0..1) brightness.
constexpr float TARGET_MEAN = 0.5f;
// Gamma clamp: <1 lifts shadows (dark images), >1 darkens only mildly.
constexpr float MIN_GAMMA = 0.6f;
constexpr float MAX_GAMMA = 1.2f;
// Gammas this close to 1 are snapped to 1 (keeps well-exposed images bit-identical).
constexpr float GAMMA_DEAD_ZONE = 0.1f;

struct Histogram {
  uint32_t bins[HISTOGRAM_BINS];
  uint32_t total;

  void clear();
  void add(uint8_t gray) {
    bins[gray >> BIN_SHIFT]++;
    total++;
  }
};

struct Result {
  uint8_t blackPoint;
  uint8_t whitePoint;
  uint16_t gammaX100;
  bool identity;  // lut[i] == i for every i: callers should skip the lookup
};

// Heap scratch for one decode: the histogram, then the LUT built from it.
struct Scratch {
  Histogram hist;
  uint8_t lut[LUT_SIZE];
};

// Always fills `lut` (identity when no adjustment is warranted).
Result buildLut(const Histogram& hist, uint8_t lut[LUT_SIZE]);

}  // namespace ToneCurve
