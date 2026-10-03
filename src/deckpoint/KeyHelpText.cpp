#include "KeyHelpText.h"

#include <cstring>

namespace deckpoint {

namespace {

// UTF-8 for « » ‹ ›.
constexpr const char* DECORATIONS[] = {"\xC2\xAB", "\xC2\xBB", "\xE2\x80\xB9", "\xE2\x80\xBA"};

size_t decorationAt(const char* s) {
  for (const char* deco : DECORATIONS) {
    const size_t n = strlen(deco);
    if (strncmp(s, deco, n) == 0) return n;
  }
  return 0;
}

void copyTruncated(const char* src, const size_t len, char* out, const size_t size) {
  if (size == 0) return;
  const size_t n = len < size - 1 ? len : size - 1;
  memcpy(out, src, n);
  out[n] = '\0';
}

}  // namespace

void cleanLabelInto(const char* label, char* out, const size_t size) {
  if (size == 0) return;
  out[0] = '\0';
  if (!label) return;
  size_t w = 0;
  for (const char* p = label; *p;) {
    if (const size_t skip = decorationAt(p)) {
      p += skip;
      continue;
    }
    if (w + 1 >= size) break;
    out[w++] = *p++;
  }
  out[w] = '\0';
  // Back off a dangling UTF-8 lead/continuation run left by truncation.
  if (w > 0 && (static_cast<unsigned char>(out[w - 1]) & 0x80)) {
    size_t start = w - 1;
    while (start > 0 && (static_cast<unsigned char>(out[start]) & 0xC0) == 0x80) start--;
    const unsigned char lead = static_cast<unsigned char>(out[start]);
    const size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
    if (w - start < need) {
      w = start;
      out[w] = '\0';
    }
  }
  size_t b = 0;
  while (out[b] == ' ') b++;
  size_t e = w;
  while (e > b && out[e - 1] == ' ') e--;
  if (b > 0) memmove(out, out + b, e - b);
  out[e - b] = '\0';
}

void splitLegendPart(const char* part, char* keys, const size_t keysSize, char* what, const size_t whatSize) {
  if (keysSize) keys[0] = '\0';
  if (whatSize) what[0] = '\0';
  if (!part) return;
  const char* sep = strstr(part, ": ");
  if (!sep) {
    copyTruncated(part, strlen(part), what, whatSize);
    return;
  }
  copyTruncated(part, static_cast<size_t>(sep - part), keys, keysSize);
  const char* rest = sep + 2;
  copyTruncated(rest, strlen(rest), what, whatSize);
}

int legendHelpRows(const LegendSnapshot& legend, const char* moveLabel, HelpRow* out, const int max) {
  int n = 0;
  const auto add = [&](const char* keys, const char* what) {
    if (n < max && what && *what) out[n++] = HelpRow{keys, what};
  };
  add("Esc", legend.back);
  add("Enter", legend.confirm);
  add(legend.extraKeys[0] ? legend.extraKeys : "", legend.extraWhat);
  const bool hasPrev = legend.prev[0] != '\0';
  const bool hasNext = legend.next[0] != '\0';
  const bool prevMove = legend.prevIsDirection || !hasPrev;
  const bool nextMove = legend.nextIsDirection || !hasNext;
  if (prevMove && nextMove && (legend.prevIsDirection || legend.nextIsDirection)) {
    add("j / k", moveLabel);
  } else {
    add("k", legend.prev);
    add("j", legend.next);
  }
  return n;
}

void humanizeActivityName(const char* name, char* out, const size_t size) {
  if (size == 0) return;
  size_t w = 0;
  for (size_t i = 0; name && name[i] && w + 1 < size; i++) {
    const char c = name[i];
    const bool upper = c >= 'A' && c <= 'Z';
    const char prev = i > 0 ? name[i - 1] : '\0';
    const bool prevLower = (prev >= 'a' && prev <= 'z') || (prev >= '0' && prev <= '9');
    if (upper && prevLower) {
      if (w + 2 >= size) break;
      out[w++] = ' ';
    }
    out[w++] = c;
  }
  out[w] = '\0';
}

}  // namespace deckpoint
