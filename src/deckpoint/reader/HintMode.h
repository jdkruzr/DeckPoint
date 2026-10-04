#pragma once

// DECKPOINT: hint labels for keyboard target picking (the reader's `d`
// dictionary hints; later highlights and link hints). Pure (no Arduino,
// renderer or I18n) so the host test suite drives it.
//
// Labels come from a home-row-first alphabet and are prefix-free: the first
// `singles` letters label one target each, the next letters are prefixes of
// two-letter labels. With N <= 26 every target gets one letter; above that
// as many targets as possible keep one letter (the earliest, in reading
// order) and the rest get two. Up to 26 * 26 targets are labelled.

#include <KeyEvent.h>

#include <cstdint>

namespace deckpoint::reader {

class HintLabels {
 public:
  static constexpr char ALPHABET[] = "asdfjklghqweruiopzxcvbnmty";
  static constexpr uint16_t ALPHABET_SIZE = sizeof(ALPHABET) - 1;
  static constexpr uint16_t MAX_TARGETS = ALPHABET_SIZE * ALPHABET_SIZE;
  static constexpr uint8_t MAX_LABEL_LEN = 2;

  HintLabels() = default;
  explicit HintLabels(uint16_t targets) { reset(targets); }
  // Targets beyond MAX_TARGETS stay unlabelled.
  void reset(uint16_t targets);

  uint16_t count() const { return total; }
  uint16_t singles() const { return singleCount; }
  // Writes target `index`'s label (NUL-terminated); "" when out of range.
  void label(uint16_t index, char (&out)[MAX_LABEL_LEN + 1]) const;
  // Target whose label is exactly `text`, else -1.
  int find(const char* text) const;
  // True when `index` is a target whose label starts with `prefix`.
  bool startsWith(uint16_t index, const char* prefix) const;

  // Position of `c` in ALPHABET, or -1.
  static int letterIndex(char c);

 private:
  uint16_t total = 0;
  uint16_t singleCount = 0;
};

// Narrows the labelled targets as label letters are typed.
class HintMatcher {
 public:
  enum class Result : uint8_t {
    Ignored,    // key did nothing (modifier, Enter, arrows...)
    Narrowed,   // typed text changed; more than one target still matches
    Selected,   // selected() is the chosen target
    Cancelled,  // Esc, Backspace with nothing typed, or no target matches
  };

  void begin(uint16_t targets);
  Result feed(const freeink::KeyEvent& event);

  const HintLabels& labels() const { return hintLabels; }
  // Letters typed so far ("" at the start).
  const char* typed() const { return typedText; }
  // True when target `index` still matches what was typed.
  bool matches(uint16_t index) const { return hintLabels.startsWith(index, typedText); }
  int selected() const { return selectedIndex; }

 private:
  HintLabels hintLabels;
  char typedText[HintLabels::MAX_LABEL_LEN + 1] = {};
  uint8_t typedLen = 0;
  int selectedIndex = -1;
};

// Where a hint tag sits: above its word, not on it. Labels are drawn in
// capitals (no descenders), so the tag only needs the label font's cap height
// plus padding (~10 px with the T-Deck's 11 px UI font, vs 15 px for a full
// line box). Its bottom edge sits a third of the reader font's ascender below
// the line top, which is just above where capitals start (Noto Serif: cap
// height ~2/3 of the ascender), so the word's first letter stays readable:
// lowercase bodies are untouched and a capital or ascender loses at most its
// top pixel. The rest of the tag rises into the line gap and the previous
// line's descender band. Clamped on screen.
struct HintTagRect {
  int x;
  int y;
  int width;
  int height;
};

// Cap height estimated from a font's ascender (ascender includes internal
// leading: Ubuntu caps ~0.74 of it, Noto ~0.67); rounded up so a capital never
// pokes out of its tag.
constexpr int hintLabelCapHeight(const int labelAscender) { return (labelAscender * 3 + 3) / 4; }

constexpr HintTagRect hintTagRect(const int wordX, const int wordY, const int readerAscender, const int tagW,
                                  const int tagH, const int screenW, const int screenH) {
  const int bottom = wordY + readerAscender / 3;
  int y = bottom - tagH;
  if (y > screenH - tagH) y = screenH - tagH;
  if (y < 0) y = 0;
  int x = wordX;
  if (x > screenW - tagW) x = screenW - tagW;
  if (x < 0) x = 0;
  return {x, y, tagW, tagH};
}

}  // namespace deckpoint::reader
