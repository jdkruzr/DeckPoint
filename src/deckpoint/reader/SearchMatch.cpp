#include "SearchMatch.h"

#include <cstring>

namespace deckpoint::reader {

namespace {

// Latin-1 Supplement + Latin Extended-A (U+00C0-U+017F), lower-case base
// letters. ' ' = separator (× ÷); digits stand for two-letter folds.
constexpr char LATIN_FOLD[] =
    "aaaaaa1ceeeeiiiidnooooo ouuuuy23"  // U+00C0
    "aaaaaa1ceeeeiiiidnooooo ouuuuy2y"  // U+00E0
    "aaaaaaccccccccdd"  // U+0100
    "ddeeeeeeeeeegggg"  // U+0110
    "gggghhhhiiiiiiii"  // U+0120
    "ii44jjkkklllllll"  // U+0130
    "lllnnnnnnnnnoooo"  // U+0140
    "oo55rrrrrrssssss"  // U+0150
    "ssttttttuuuuuuuu"  // U+0160
    "uuuuwwyyyzzzzzzs";  // U+0170
static_assert(sizeof(LATIN_FOLD) - 1 == 0x180 - 0xC0, "one entry per codepoint");
constexpr char LATIN_PAIRS[][2] = {{'a', 'e'}, {'t', 'h'}, {'s', 's'}, {'i', 'j'}, {'o', 'e'}};

uint32_t nextCodepoint(const uint8_t*& p, const uint8_t* end) {
  const uint8_t b = *p++;
  if (b < 0x80) return b;
  int extra;
  uint32_t cp;
  if ((b & 0xE0) == 0xC0) {
    extra = 1;
    cp = b & 0x1F;
  } else if ((b & 0xF0) == 0xE0) {
    extra = 2;
    cp = b & 0x0F;
  } else if ((b & 0xF8) == 0xF0) {
    extra = 3;
    cp = b & 0x07;
  } else {
    return 0xFFFD;  // stray continuation / invalid lead byte
  }
  for (int i = 0; i < extra; i++) {
    if (p >= end || (*p & 0xC0) != 0x80) return 0xFFFD;
    cp = (cp << 6) | (*p++ & 0x3F);
  }
  return cp;
}

bool isSeparatorAbove7F(const uint32_t cp) {
  if (cp < 0xC0) return cp != 0xAA && cp != 0xB5 && cp != 0xBA;  // ª µ º are letters
  if (cp == 0xD7 || cp == 0xF7) return true;                      // × ÷
  return (cp >= 0x2000 && cp <= 0x2BFF) ||                        // punctuation, symbols, arrows, math
         (cp >= 0x2E00 && cp <= 0x2E7F) ||                        // supplemental punctuation
         (cp >= 0x3000 && cp <= 0x303F) ||                        // CJK punctuation
         (cp >= 0xFE10 && cp <= 0xFE6F) ||                        // vertical / small forms
         (cp >= 0xFF01 && cp <= 0xFF0F) || (cp >= 0xFF1A && cp <= 0xFF20) || (cp >= 0xFF3B && cp <= 0xFF40) ||
         (cp >= 0xFF5B && cp <= 0xFF65) || cp == 0xFFFD;
}

bool isIgnorable(const uint32_t cp) {
  return cp == 0xAD ||                      // soft hyphen
         (cp >= 0x300 && cp <= 0x36F) ||    // combining accents
         (cp >= 0x200B && cp <= 0x200F) ||  // zero-width space / joiners / marks
         cp == 0x2060 || cp == 0xFEFF;
}

// Scripts written without spaces: every character starts a word.
bool isIdeograph(const uint32_t cp) {
  return (cp >= 0x3040 && cp <= 0x30FF) || (cp >= 0x3400 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
         cp >= 0x20000;
}

}  // namespace

uint8_t foldCodepoint(uint32_t cp, uint32_t out[2], bool* separator) {
  *separator = false;
  if (cp < 0x80) {
    if (cp >= 'A' && cp <= 'Z') cp += 'a' - 'A';
    if ((cp >= 'a' && cp <= 'z') || (cp >= '0' && cp <= '9')) {
      out[0] = cp;
      return 1;
    }
    *separator = true;
    return 0;
  }
  if (isIgnorable(cp)) return 0;
  if (cp >= 0xC0 && cp < 0x180) {
    const char c = LATIN_FOLD[cp - 0xC0];
    if (c == ' ') {
      *separator = true;
      return 0;
    }
    if (c >= '1' && c <= '5') {
      out[0] = static_cast<uint8_t>(LATIN_PAIRS[c - '1'][0]);
      out[1] = static_cast<uint8_t>(LATIN_PAIRS[c - '1'][1]);
      return 2;
    }
    out[0] = static_cast<uint8_t>(c);
    return 1;
  }
  if (isSeparatorAbove7F(cp)) {
    *separator = true;
    return 0;
  }
  if (cp == 0xAA) cp = 'a';
  if (cp == 0xBA) cp = 'o';
  // Greek and Cyrillic capitals; final sigma folds to sigma.
  if ((cp >= 0x391 && cp <= 0x3A9) || (cp >= 0x410 && cp <= 0x42F)) {
    cp += 0x20;
  } else if (cp >= 0x400 && cp <= 0x40F) {
    cp += 0x50;
  } else if (cp == 0x3C2) {
    cp = 0x3C3;
  }
  out[0] = cp;
  return 1;
}

bool endsWithHyphenBreak(const char* text, const size_t len) {
  if (len < 2 || text[len - 1] != '-') return false;
  // The byte before the hyphen ends a letter: ASCII letter or the tail of a
  // multi-byte character (accented letters; punctuation tokens are split off).
  const auto prev = static_cast<uint8_t>(text[len - 2]);
  return ((prev | 0x20) >= 'a' && (prev | 0x20) <= 'z') || prev >= 0x80;
}

bool SearchQuery::set(const char* utf8) {
  len = 0;
  if (!utf8) return false;
  const auto* p = reinterpret_cast<const uint8_t*>(utf8);
  const auto* end = p + strlen(utf8);
  while (p < end) {
    uint32_t units[2];
    bool separator;
    const uint8_t n = foldCodepoint(nextCodepoint(p, end), units, &separator);
    if (len + n > MAX_UNITS) break;
    for (uint8_t i = 0; i < n; i++) unitBuf[len++] = units[i];
  }
  return len > 0;
}

void PhraseMatcher::begin(const SearchQuery* q) {
  query = q;
  reset();
}

void PhraseMatcher::reset() {
  state[0] = state[1] = 0;
  position = 0;
  afterSeparator = true;
}

bool PhraseMatcher::step(const uint32_t unit, const bool wordStart, const uint32_t token) {
  const uint8_t m = query->length();
  const uint32_t* q = query->units();
  ring[position % RING] = token;
  position++;

  // state = ((state << 1) | start) & mask(unit)
  uint64_t lo = (state[0] << 1) | (wordStart ? 1u : 0u);
  uint64_t hi = (state[1] << 1) | (state[0] >> 63);
  uint64_t maskLo = 0;
  uint64_t maskHi = 0;
  for (uint8_t j = 0; j < m; j++) {
    if (q[j] != unit) continue;
    if (j < 64) {
      maskLo |= uint64_t{1} << j;
    } else {
      maskHi |= uint64_t{1} << (j - 64);
    }
  }
  lo &= maskLo;
  hi &= maskHi;
  state[0] = lo;
  state[1] = hi;
  const uint8_t last = m - 1;
  return last < 64 ? ((lo >> last) & 1) != 0 : ((hi >> (last - 64)) & 1) != 0;
}

bool PhraseMatcher::feed(const char* text, const size_t len, const uint32_t token, const bool continuesWord) {
  if (!query || query->length() == 0 || !text) return false;
  // A token boundary separates words unless the token continues a hyphenated one.
  afterSeparator = !continuesWord;
  bool matched = false;
  const auto* p = reinterpret_cast<const uint8_t*>(text);
  const auto* end = p + len;
  while (p < end) {
    const uint32_t cp = nextCodepoint(p, end);
    uint32_t units[2];
    bool separator;
    const uint8_t n = foldCodepoint(cp, units, &separator);
    if (separator) {
      afterSeparator = true;
      continue;
    }
    const bool ideograph = isIdeograph(cp);
    for (uint8_t i = 0; i < n; i++) {
      const bool wordStart = i == 0 && (afterSeparator || ideograph);
      if (step(units[i], wordStart, token) && !matched) {
        matched = true;
        lastHit.firstToken = ring[(position - query->length()) % RING];
        lastHit.lastToken = token;
      }
    }
    if (n > 0) afterSeparator = false;
  }
  return matched;
}

}  // namespace deckpoint::reader
