#pragma once

// DECKPOINT: dependency-free text helpers behind the key help screen, kept
// apart from the renderer and I18n so the host test suite can exercise them.

#include <cstddef>

namespace deckpoint {

// The last bottom key legend drawn (drawHintLegend inputs), tagged with the
// activity that drew it. Fixed buffers: recorded on every legend draw.
struct LegendSnapshot {
  char owner[32];
  char back[32];
  char confirm[32];
  char prev[32];
  char next[32];
  char extraKeys[16];
  char extraWhat[32];
  bool prevIsDirection;
  bool nextIsDirection;
};

// One rendered help row; both pointers borrow storage owned by the caller.
struct HelpRow {
  const char* keys;
  const char* what;
};

// Copy `label` into `out`, dropping the « » ‹ › hint decorations and
// surrounding spaces. Always NUL-terminates (truncating if needed).
void cleanLabelInto(const char* label, char* out, size_t size);

// Split a legend part such as "h/l: tab" at its first ": " into keys and
// meaning. A part without ": " is all meaning.
void splitLegendPart(const char* part, char* keys, size_t keysSize, char* what, size_t whatSize);

// Translate a recorded legend into key terms: Esc = back label, Enter =
// confirm label, the extra part, then k / j for previous / next (one
// "j / k" = `moveLabel` row when both are plain directions). Returns the
// number of rows written (<= max). Rows point into `legend` and `moveLabel`.
int legendHelpRows(const LegendSnapshot& legend, const char* moveLabel, HelpRow* out, int max);

// "WifiSelection" -> "Wifi Selection": readable title for activities without
// a translated one. Always NUL-terminates.
void humanizeActivityName(const char* name, char* out, size_t size);

}  // namespace deckpoint
