// DECKPOINT: reader touch that lives alongside the keyboard.
//
// Precedence, checked in this order from EpubReaderActivity::loop():
//  1. A keyboard mode on screen (`:` / `/` line, a running search, `d` hint
//     labels, the inverted search hit) owns the page: any finished touch (tap,
//     swipe, long-press) cancels it, restoring the clean page, and does
//     nothing else.
//  2. The toolbar overlay / end-of-book menu own their own touch.
//  3. A long-press claims its contact, so its lift is never a page turn, a
//     long-tap chapter skip or the menu tap; the word under the finger gets
//     the Look up / Highlight / Note popup (a highlighted word: Edit note /
//     Delete / Look up), blank space does nothing.
//  4. Link taps, then a tap on a highlighted word (its popup), then the
//     center menu tap / top-edge swipe, then the page-turn tap zones and
//     swipes (CrossPoint).
//  A selection opened by touch (EpubReaderSelection.cpp) takes the taps that
//  follow: popup rows, the end word, the Cancel / Save bar.

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "activities/RenderLock.h"
#include "activities/reader/EpubReaderActivity.h"
#include "deckpoint/reader/PageWords.h"
#include "deckpoint/reader/WordHitTest.h"

using deckpoint::CommandResult;

namespace {

// A plain tap only opens a highlight's popup when it lands (nearly) on one of
// its words; anything looser would steal page-turn taps next to highlights.
constexpr int HIGHLIGHT_TAP_SLOP_PX = 3;

}  // namespace

bool EpubReaderActivity::keyModeTouchTick() {
  if (!mappedInput.hasTouchInput()) return false;
  const bool selecting = hintsOpen && hintsSelect;
  if (selecting ? !readerOwnsPage() : !wantsRawKeys()) return false;
  const bool modeUp = search || hintsOpen || cmdLine.isOpen() || searchMark.active;
  if (!modeUp) return false;

  int x = 0;
  int y = 0;
  bool touched = mappedInput.wasScreenTapped(x, y);
  if (!touched && mappedInput.wasSwipe() != MappedInputManager::SwipeDir::None) {
    touched = true;
    x = -1;
    y = -1;
  }
  // wasScreenLongPress() claims the rest of the contact: its lift must not act
  // on the restored page.
  if (!touched && mappedInput.wasScreenLongPress(x, y)) touched = true;
  if (!touched) return false;

  readerKeys.reset();
  pendingManualTurn = 0;
  if (selecting) {
    // A selection takes taps itself (popup rows, end word); its keyboard
    // label stages are cancelled like the other keyboard modes.
    selectionTouch(x, y);
    if (searchMark.active) clearSearchMark(true);
    return true;
  }

  LOG_DBG("ERS", "Touch cancels keyboard mode");
  if (search) {
    finishSearch(SearchStep::Cancelled, nullptr);  // closes the `/` line with it
  } else if (hintsOpen) {
    closeHints(true);
  } else if (cmdLine.isOpen()) {
    closeCommandLine(CommandResult::Restore);
  }
  if (searchMark.active) clearSearchMark(true);
  return true;
}

bool EpubReaderActivity::touchWordMenuTick() {
  if (!SETTINGS.touchReaderControls || !mappedInput.touchGestureEnabled(deckpoint::touch::GESTURE_WORD_LOOKUP) ||
      !section) {
    return false;
  }
  int x = 0;
  int y = 0;
  if (!mappedInput.wasScreenLongPress(x, y)) return false;  // claims the contact
  pendingManualTurn = 0;
  bool rerender = false;
  {
    RenderLock lock;
    // Page + word boxes (~2 KB for a full page), held while the popup is up;
    // freed right away when the press found no word.
    auto session = loadHintSessionLocked();
    if (!session) return true;
    const int hit = deckpoint::reader::findWordAt(session->words.data(), session->words.size(), x, y,
                                                  renderer.getLineHeight(SETTINGS.getReaderFontId()),
                                                  deckpoint::reader::WORD_TOUCH_SLOP_PX);
    if (hit < 0) {
      LOG_DBG("ERS", "Long-press at %d,%d: no word", x, y);
      return true;
    }
    LOG_DBG("ERS", "Long-press '%s' at %d,%d", session->words[hit].text, x, y);
    rerender = openTouchSelectionLocked(std::move(session), hit);
  }
  if (rerender) requestUpdate();
  return true;
}

bool EpubReaderActivity::touchHighlightTapTick() {
  if (!pageHasHighlightMarks || !annotationStore || !section || !SETTINGS.touchReaderControls ||
      !mappedInput.hasTouchInput()) {
    return false;
  }
  int x = 0;
  int y = 0;
  if (!mappedInput.wasScreenTapped(x, y)) return false;
  bool rerender = false;
  {
    RenderLock lock;
    // Transient on pages that show highlights only: a tap off the highlights
    // frees it and falls through to the page-turn zones.
    auto session = loadHintSessionLocked();
    if (!session) return false;
    const int hit =
        deckpoint::reader::findWordAt(session->words.data(), session->words.size(), x, y,
                                      renderer.getLineHeight(SETTINGS.getReaderFontId()), HIGHLIGHT_TAP_SLOP_PX);
    if (hit < 0 || annotationStore->highlightAt(currentSpineIndex, session->words[hit].visibleOffset) < 0) {
      return false;
    }
    LOG_DBG("ERS", "Tap on highlight at '%s'", session->words[hit].text);
    rerender = openTouchSelectionLocked(std::move(session), hit);
  }
  if (rerender) requestUpdate();
  return true;
}
