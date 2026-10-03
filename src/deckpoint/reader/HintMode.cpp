#include "HintMode.h"

namespace deckpoint::reader {

int HintLabels::letterIndex(const char c) {
  for (uint16_t i = 0; i < ALPHABET_SIZE; i++) {
    if (ALPHABET[i] == c) return i;
  }
  return -1;
}

void HintLabels::reset(const uint16_t targets) {
  total = targets > MAX_TARGETS ? MAX_TARGETS : targets;
  if (total <= ALPHABET_SIZE) {
    singleCount = total;
    return;
  }
  // s singles leave (K - s) prefixes of K labels each: s + (K - s) * K >= N
  // holds for the largest s = (K*K - N) / (K - 1).
  singleCount = static_cast<uint16_t>((MAX_TARGETS - total) / (ALPHABET_SIZE - 1));
}

void HintLabels::label(const uint16_t index, char (&out)[MAX_LABEL_LEN + 1]) const {
  out[0] = '\0';
  if (index >= total) return;
  if (index < singleCount) {
    out[0] = ALPHABET[index];
    out[1] = '\0';
    return;
  }
  const uint16_t rest = index - singleCount;
  out[0] = ALPHABET[singleCount + rest / ALPHABET_SIZE];
  out[1] = ALPHABET[rest % ALPHABET_SIZE];
  out[2] = '\0';
}

int HintLabels::find(const char* text) const {
  if (text == nullptr || text[0] == '\0') return -1;
  const int first = letterIndex(text[0]);
  if (first < 0) return -1;
  if (text[1] == '\0') return first < singleCount ? first : -1;
  if (text[2] != '\0' || first < singleCount) return -1;
  const int second = letterIndex(text[1]);
  if (second < 0) return -1;
  const int index = singleCount + (first - singleCount) * ALPHABET_SIZE + second;
  return index < total ? index : -1;
}

bool HintLabels::startsWith(const uint16_t index, const char* prefix) const {
  if (index >= total) return false;
  char text[MAX_LABEL_LEN + 1];
  label(index, text);
  for (uint8_t i = 0; prefix[i] != '\0'; i++) {
    if (i >= MAX_LABEL_LEN || text[i] != prefix[i]) return false;
  }
  return true;
}

void HintMatcher::begin(const uint16_t targets) {
  hintLabels.reset(targets);
  typedText[0] = '\0';
  typedLen = 0;
  selectedIndex = -1;
}

HintMatcher::Result HintMatcher::feed(const freeink::KeyEvent& event) {
  using freeink::SpecialKey;
  switch (event.special) {
    case SpecialKey::None:
      break;
    case SpecialKey::Escape:
      return Result::Cancelled;
    case SpecialKey::Backspace:
      if (typedLen == 0) return Result::Cancelled;
      typedText[--typedLen] = '\0';
      return Result::Narrowed;
    default:
      return Result::Ignored;
  }

  char c = event.ch;
  if (c == 0) return Result::Ignored;
  if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  if (HintLabels::letterIndex(c) < 0 || typedLen >= HintLabels::MAX_LABEL_LEN) return Result::Cancelled;

  typedText[typedLen++] = c;
  typedText[typedLen] = '\0';
  const int exact = hintLabels.find(typedText);
  if (exact >= 0) {
    selectedIndex = exact;
    return Result::Selected;
  }
  // Labels are prefix-free, so a non-exact match is a two-letter prefix:
  // any target behind it keeps the hint open.
  if (typedLen == 1) {
    const int first = HintLabels::letterIndex(c);
    const int firstTwoLetter = hintLabels.singles();
    if (first >= firstTwoLetter &&
        firstTwoLetter + (first - firstTwoLetter) * HintLabels::ALPHABET_SIZE < hintLabels.count()) {
      return Result::Narrowed;
    }
  }
  return Result::Cancelled;
}

}  // namespace deckpoint::reader
