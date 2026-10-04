#pragma once

#include <KeyEvent.h>

namespace deckpoint {

// Key help opens on '?' (Sym+v) and on Alt+v: Alt and Sym sit side by side and Alt+v has no
// other binding, so the near-miss also works.
inline bool isHelpKey(const freeink::KeyEvent& event) {
  if (event.ch == '?') return true;
  const bool alt = (event.mods & freeink::KeyMod::Alt) != 0;
  return alt && (event.ch == 'v' || event.ch == 'V');
}

}  // namespace deckpoint
