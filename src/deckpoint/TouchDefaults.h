#pragma once

// DECKPOINT: reader touch defaults per board and their one-time migration
// (pure, host-testable).
//
// The keyboard hybrid (T-Deck Pro) turns pages by tap zones and swipes and
// opens the menu with a center tap; touch-first boards keep CrossPoint's
// swipe-only defaults. Values mirror CrossPointSettings' PAGE_TURN_GESTURE /
// SHOW_READER_MENU / TOUCH_READER_CONTROLS enums (static_asserts in
// CrossPointSettings.cpp keep them in step).
//
// Stored settings win over struct defaults, so a file written before these
// defaults existed would keep SWIPE_ONLY forever. On the hybrid board,
// "deckpointSettings" in the settings file records which revision of the
// DeckPoint defaults the file has seen; a file below SETTINGS_REVISION gets
// the hybrid reader defaults once, then is stamped and the user's later
// choices stick. Other boards neither migrate nor stamp (their files stay
// byte-for-byte CrossPoint's).

#include <cstdint>

namespace deckpoint::touch {

// Revision 1: hybrid reader touch defaults (Phase T).
constexpr uint8_t SETTINGS_REVISION = 1;

constexpr uint8_t GESTURE_TAP_AND_SWIPE = 0;
constexpr uint8_t GESTURE_SWIPE_ONLY = 2;
constexpr uint8_t MENU_TAP = 1;
constexpr uint8_t READER_TOUCH_ON = 1;

struct ReaderTouchSettings {
  uint8_t& touchReaderControls;
  uint8_t& pageTurnGesture;
  uint8_t& previousPageGesture;
  uint8_t& showReaderMenu;
};

// Seeds the board's defaults (before a settings load, so a stored value wins).
inline void applyReaderTouchDefaults(const bool keyboardHybrid, const ReaderTouchSettings s) {
  if (!keyboardHybrid) return;
  s.touchReaderControls = READER_TOUCH_ON;
  s.pageTurnGesture = GESTURE_TAP_AND_SWIPE;
  s.previousPageGesture = GESTURE_TAP_AND_SWIPE;
  s.showReaderMenu = MENU_TAP;
}

// After a load on the hybrid board: true when the file predates
// SETTINGS_REVISION and got the hybrid defaults over its stored values (resave
// to stamp it, so this happens at most once).
inline bool migrateReaderTouchSettings(const uint8_t storedRevision, const bool keyboardHybrid,
                                       const ReaderTouchSettings s) {
  if (!keyboardHybrid || storedRevision >= SETTINGS_REVISION) return false;
  applyReaderTouchDefaults(keyboardHybrid, s);
  return true;
}

}  // namespace deckpoint::touch
