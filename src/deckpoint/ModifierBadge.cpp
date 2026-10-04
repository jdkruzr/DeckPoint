#include "ModifierBadge.h"

#include <cstring>

namespace deckpoint {

bool ModifierDebounce::update(const uint8_t live, const unsigned long nowMs) {
  if (live == shown_) {
    pending_ = live;
    return false;
  }
  if ((live & ~shown_) == 0) {  // only removals: show at once
    shown_ = pending_ = live;
    return true;
  }
  if (live != pending_) {
    pending_ = live;
    since_ = nowMs;
    return false;
  }
  if (nowMs - since_ < SETTLE_MS) return false;
  shown_ = live;
  return true;
}

size_t formatModifierBadge(const uint8_t bits, const ModifierNames& names, char* out, const size_t outSize) {
  if (outSize == 0) return 0;
  out[0] = '\0';
  size_t len = 0;
  for (int i = 0; i < 5; i++) {
    const char* name = names.names[i];
    if (!(bits & (1u << i)) || name == nullptr || name[0] == '\0') continue;
    const size_t nameLen = strlen(name);
    const size_t sep = len > 0 ? 1 : 0;
    if (len + sep + nameLen >= outSize) break;
    if (sep) out[len++] = ' ';
    memcpy(out + len, name, nameLen);
    len += nameLen;
    out[len] = '\0';
  }
  return len;
}

}  // namespace deckpoint
