#include "ToneCurve.h"

#include <cstring>

namespace ToneCurve {

namespace {

void fillIdentity(uint8_t lut[LUT_SIZE]) {
  for (int i = 0; i < LUT_SIZE; i++) lut[i] = static_cast<uint8_t>(i);
}

Result identityResult() { return {0, 255, 100, true}; }

// Small ln/exp so the curve does not pull libm's powf/logf (~2.3 KB flash)
// into firmware; accuracy (~1e-5) is far below one 8-bit output step.
constexpr float LN2 = 0.69314718f;

// Natural log for x > 0: normalise to [1, 2), then 2*atanh((m-1)/(m+1)).
float lnApprox(float x) {
  int exponent = 0;
  while (x >= 2.0f) {
    x *= 0.5f;
    exponent++;
  }
  while (x < 1.0f) {
    x *= 2.0f;
    exponent--;
  }
  const float s = (x - 1.0f) / (x + 1.0f);
  const float s2 = s * s;
  const float series = s * (1.0f + s2 * (1.0f / 3.0f + s2 * (1.0f / 5.0f + s2 * (1.0f / 7.0f))));
  return 2.0f * series + static_cast<float>(exponent) * LN2;
}

// e^y for y <= 0 (all callers raise a value in (0, 1] to a positive power).
float expNonPositive(float y) {
  if (y < -30.0f) return 0.0f;
  int halvings = 0;
  while (y < -LN2) {
    y += LN2;
    halvings++;
  }
  // y in [-ln2, 0]: Taylor series to y^7.
  float term = 1.0f;
  float result = 1.0f;
  for (int k = 1; k <= 7; k++) {
    term *= y / static_cast<float>(k);
    result += term;
  }
  while (halvings-- > 0) result *= 0.5f;
  return result;
}

// First bin whose cumulative count reaches `threshold` samples.
int percentileBin(const Histogram& hist, uint32_t threshold) {
  uint32_t cumulative = 0;
  for (int i = 0; i < HISTOGRAM_BINS; i++) {
    cumulative += hist.bins[i];
    if (cumulative >= threshold) return i;
  }
  return HISTOGRAM_BINS - 1;
}

}  // namespace

void Histogram::clear() {
  memset(bins, 0, sizeof(bins));
  total = 0;
}

Result buildLut(const Histogram& hist, uint8_t lut[LUT_SIZE]) {
  fillIdentity(lut);
  if (hist.total < MIN_SAMPLES) return identityResult();

  constexpr int BIN_WIDTH = 1 << BIN_SHIFT;
  const uint64_t total = hist.total;
  const uint32_t blackThreshold = static_cast<uint32_t>((total * BLACK_PERMILLE + 999) / 1000);
  const uint32_t whiteThreshold = static_cast<uint32_t>((total * WHITE_PERMILLE + 999) / 1000);
  // Black point at its bin's low edge, white point at its bin's high edge, so
  // binning never clips more than the percentile asks for.
  const int black = percentileBin(hist, blackThreshold < 1 ? 1 : blackThreshold) * BIN_WIDTH;
  const int white = percentileBin(hist, whiteThreshold) * BIN_WIDTH + BIN_WIDTH - 1;
  const int range = white - black;
  if (range < MIN_RANGE) return identityResult();

  float gain = 1.0f;
  float offset = 0.0f;  // output value (0..255) that `black` maps to
  const bool fullRange = black <= FULL_RANGE_SLACK && white >= 255 - FULL_RANGE_SLACK;
  if (!fullRange) {
    gain = 255.0f / static_cast<float>(range);
    if (gain > MAX_GAIN) {
      // Capped: split the unused output range in proportion to the input margins,
      // so a mostly-dark low-contrast image stays dark-ish rather than recentred.
      gain = MAX_GAIN;
      const float slack = 255.0f - static_cast<float>(range) * gain;
      const float margins = static_cast<float>(black + (255 - white));
      offset = margins > 0.0f ? slack * static_cast<float>(black) / margins : slack * 0.5f;
    }
  } else {
    offset = static_cast<float>(black);  // full range: pass through unchanged
  }

  const auto stretch = [black, gain, offset](float v) {
    float s = (v - static_cast<float>(black)) * gain + offset;
    if (s < 0.0f) s = 0.0f;
    if (s > 255.0f) s = 255.0f;
    return s / 255.0f;
  };

  // Post-stretch mean from bin centres.
  double weighted = 0.0;
  for (int i = 0; i < HISTOGRAM_BINS; i++) {
    if (hist.bins[i] == 0) continue;
    const float centre = static_cast<float>(i * BIN_WIDTH) + (BIN_WIDTH - 1) * 0.5f;
    weighted += static_cast<double>(hist.bins[i]) * stretch(centre);
  }
  float mean = static_cast<float>(weighted / static_cast<double>(total));
  if (mean < 0.01f) mean = 0.01f;
  if (mean > 0.99f) mean = 0.99f;

  float gamma = lnApprox(TARGET_MEAN) / lnApprox(mean);
  if (gamma < MIN_GAMMA) gamma = MIN_GAMMA;
  if (gamma > MAX_GAMMA) gamma = MAX_GAMMA;
  if (gamma > 1.0f - GAMMA_DEAD_ZONE && gamma < 1.0f + GAMMA_DEAD_ZONE) gamma = 1.0f;

  bool identity = true;
  for (int i = 0; i < LUT_SIZE; i++) {
    float v = stretch(static_cast<float>(i));
    if (gamma != 1.0f && v > 0.0f) v = expNonPositive(gamma * lnApprox(v));
    int out = static_cast<int>(v * 255.0f + 0.5f);
    if (out > 255) out = 255;
    lut[i] = static_cast<uint8_t>(out);
    if (out != i) identity = false;
  }

  return {static_cast<uint8_t>(black), static_cast<uint8_t>(white), static_cast<uint16_t>(gamma * 100.0f + 0.5f),
          identity};
}

}  // namespace ToneCurve
