// DECKPOINT: firmware side of ModifierBadge (shared shown state + i18n labels).
#include <I18n.h>

#include <atomic>

#include "ModifierBadge.h"

namespace deckpoint {
namespace {
ModifierDebounce s_debounce;
// Written by the main loop, read by the render task.
std::atomic<uint8_t> s_shown{0};
}  // namespace

uint8_t shownModifiers() { return s_shown.load(); }

const char* modifierBadgeText(const uint8_t mask) {
  static char text[48];
  const uint8_t bits = s_shown.load() & mask;
  if (bits == 0) return "";
  const ModifierNames names{
      {tr(STR_MOD_SHIFT), tr(STR_MOD_CAPS), tr(STR_MOD_SYM), tr(STR_MOD_SYM_LOCK), tr(STR_MOD_ALT)}};
  formatModifierBadge(bits, names, text, sizeof(text));
  return text;
}

bool pollModifierBadge(const uint8_t live, const unsigned long nowMs, uint8_t* before) {
  const uint8_t old = s_debounce.shown();
  if (!s_debounce.update(live, nowMs)) return false;
  s_shown.store(s_debounce.shown());
  if (before) *before = old;
  return true;
}

}  // namespace deckpoint
