#pragma once

#include "KeyMic.h"

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

// Optional second section whose key column is composed at runtime (e.g. from
// a command registry). row() writes row `index`'s keys into `keys`
// (NUL-terminated) and returns what it does.
struct KeyHelpExtra {
  StrId title;
  uint8_t count;
  StrId (*row)(uint8_t index, char* keys, size_t keysSize);
};

struct KeyHelp {
  const KeyHelpEntry* entries;
  uint8_t count;
  const KeyHelpExtra* extra;
};

template <size_t N>
constexpr KeyHelp makeKeyHelp(const KeyHelpEntry (&entries)[N], const KeyHelpExtra* extra = nullptr) {
  static_assert(N <= UINT8_MAX, "key help table too long");
  return KeyHelp{entries, static_cast<uint8_t>(N), extra};
}

// When a touch help row applies; KeyHelpActivity checks the live reader
// settings so the page never describes a gesture that is switched off.
enum class TouchWhen : uint8_t {
  Always,
  TapZones,     // both directions tap-enabled, not inverted: left third / right side
  PageSwipe,    // either direction swipe-enabled
  MenuTap,      // Show reader menu = Tap
  ReaderTouch,  // touch reader controls on
};

// A touch help row: both columns translated (gestures are words, not caps).
struct TouchHelpEntry {
  StrId gesture;
  StrId what;
  TouchWhen when;
};

struct TouchHelp {
  StrId title;
  const TouchHelpEntry* entries;
  uint8_t count;
};

template <size_t N>
constexpr TouchHelp makeTouchHelp(const StrId title, const TouchHelpEntry (&entries)[N]) {
  static_assert(N <= UINT8_MAX, "touch help table too long");
  return TouchHelp{title, entries, static_cast<uint8_t>(N)};
}

// Touch section of the `?` page, shown while touch input is on: the book one
// in the reader, the lists one everywhere else.
extern const TouchHelp TOUCH_BOOK_HELP;
extern const TouchHelp TOUCH_LISTS_HELP;

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
