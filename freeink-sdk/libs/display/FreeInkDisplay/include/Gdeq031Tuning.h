#pragma once

// DECKPOINT: live tuning for the GDEQ031T10 (T-Deck Pro) grayscale waveform.
//
// There is no published 4-gray waveform for this panel, so the driver builds a
// minimal "nudge" LUT: after the B/W base shows every gray pixel black, gray
// pixels get a short VSL (toward-white) drive. Frame counts set how far they
// travel: more frames = lighter. These setters let firmware/dev tooling
// calibrate on the glass without reflashing (serial: CMD:GRAY:<light>,<dark> for text,
// CMD:GRAYIMG:<light>,<dark> for images).

#include <stdint.h>

namespace freeink {
// Two calibrations, picked by the refresh that drew the B/W base: a deep (standard
// waveform) base leaves blacker black than the fast one, so the same nudge lands darker.
//   Text  - fast base (reader pages, UI): default 3/1
//   Image - deep base (image viewer):     default 4/2
enum class Gdeq031GrayProfile : uint8_t { Text = 0, Image = 1 };
void gdeq031SetGrayFrames(uint8_t lightFrames, uint8_t darkFrames,
                          Gdeq031GrayProfile profile = Gdeq031GrayProfile::Text);
void gdeq031GetGrayFrames(uint8_t& lightFrames, uint8_t& darkFrames,
                          Gdeq031GrayProfile profile = Gdeq031GrayProfile::Text);
// Group/state repeat byte for the gray groups (0 = run once; each step up
// repeats the group and its states, multiplying the drive). Finer frame
// resolution at 0. Serial: CMD:GRAY:<light>,<dark>,<repeat>.
void gdeq031SetGrayRepeat(uint8_t repeat);
uint8_t gdeq031GrayRepeat();
}  // namespace freeink
