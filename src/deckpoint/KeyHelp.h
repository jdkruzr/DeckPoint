#pragma once

// DECKPOINT: per-screen keyboard help shown by the global `?` key
// (KeyHelpActivity). Screens with a hand-written table return it from
// Activity::keyHelp(); every other screen falls back to its last key legend.

#include <I18nKeys.h>

#include <cstddef>
#include <cstdint>

namespace deckpoint {

// One help row: the key(s) as printed on the caps, and what they do here.
struct KeyHelpEntry {
  const char* keys;
  StrId what;
};

struct KeyHelp {
  const KeyHelpEntry* entries;
  uint8_t count;
};

template <size_t N>
constexpr KeyHelp makeKeyHelp(const KeyHelpEntry (&entries)[N]) {
  static_assert(N <= UINT8_MAX, "key help table too long");
  return KeyHelp{entries, static_cast<uint8_t>(N)};
}

// Hand-written tables (KeyHelpTables.cpp, flash-resident).
extern const KeyHelp HOME_KEY_HELP;
extern const KeyHelp LIBRARY_KEY_HELP;
extern const KeyHelp FILE_BROWSER_KEY_HELP;
extern const KeyHelp SETTINGS_KEY_HELP;
extern const KeyHelp TEXT_SETTINGS_KEY_HELP;
extern const KeyHelp READER_KEY_HELP;
extern const KeyHelp READER_MENU_KEY_HELP;
extern const KeyHelp BOOKMARKS_KEY_HELP;
extern const KeyHelp WORD_SELECT_KEY_HELP;
extern const KeyHelp DEFINITION_KEY_HELP;
extern const KeyHelp KEYBOARD_ENTRY_KEY_HELP;
extern const KeyHelp IMAGE_VIEWER_KEY_HELP;
extern const KeyHelp GO_TO_PERCENT_KEY_HELP;

}  // namespace deckpoint
