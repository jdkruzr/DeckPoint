// DECKPOINT: reader touch that lives alongside the keyboard.
//
// Precedence, checked in this order from EpubReaderActivity::loop():
//  1. A keyboard mode on screen (`:` / `/` line, a running search, `d` hint
//     labels, the inverted search hit) owns the page: any finished touch (tap,
//     swipe, long-press) cancels it, restoring the clean page, and does
//     nothing else.
//  2. The toolbar overlay / end-of-book menu own their own touch.
//  3. A long-press claims its contact, so its lift is never a page turn, a
//     long-tap chapter skip or the menu tap; the word under the finger is
//     looked up, blank space does nothing.
//  4. Link taps, then the center menu tap / top-edge swipe, then the
//     page-turn tap zones and swipes (CrossPoint).

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>
#include <vector>

#include "CrossPointSettings.h"
#include "activities/RenderLock.h"
#include "activities/reader/EpubReaderActivity.h"
#include "deckpoint/reader/PageWords.h"
#include "deckpoint/reader/WordHitTest.h"

using deckpoint::CommandResult;
using deckpoint::reader::WordBox;

namespace {

// How far outside a word box a fingertip still picks it (logical px). Below
// half the T-Deck's ~17 px line gap pitch, so a press between two lines goes
// to the nearer one rather than skipping both.
constexpr int WORD_TOUCH_SLOP_PX = 8;
// Longest word copied out for the lookup (bytes, incl. NUL).
constexpr size_t MAX_LOOKUP_WORD = 64;

}  // namespace

bool EpubReaderActivity::keyModeTouchTick() {
  if (!mappedInput.hasTouchInput() || !wantsRawKeys()) return false;
  const bool modeUp = search || hintsOpen || cmdLine.isOpen() || searchMark.active;
  if (!modeUp) return false;

  int x = 0;
  int y = 0;
  bool touched = mappedInput.wasScreenTapped(x, y) || mappedInput.wasSwipe() != MappedInputManager::SwipeDir::None;
  // wasScreenLongPress() claims the rest of the contact: its lift must not act
  // on the restored page.
  if (!touched && mappedInput.wasScreenLongPress(x, y)) touched = true;
  if (!touched) return false;

  LOG_DBG("ERS", "Touch cancels keyboard mode");
  readerKeys.reset();
  pendingManualTurn = 0;
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

bool EpubReaderActivity::touchLookUpTick() {
  if (!SETTINGS.touchReaderControls ||
      !mappedInput.touchGestureEnabled(deckpoint::touch::GESTURE_WORD_LOOKUP) || !section) {
    return false;
  }
  int x = 0;
  int y = 0;
  if (!mappedInput.wasScreenLongPress(x, y)) return false;  // claims the contact
  pendingManualTurn = 0;
  if (SETTINGS.dictionaryName[0] == '\0') {
    showKeyPopup(tr(STR_DICT_NO_DICT_SET), true);  // as `d`: no page read for nothing
    return true;
  }

  char word[MAX_LOOKUP_WORD];
  word[0] = '\0';
  {
    RenderLock lock;
    if (!section || !renderer.hasFrameBuffer()) return true;
    // Page + word boxes (~2 KB for a full page; extractPageWords reserves the
    // vector), freed after this hit test. The same transient footprint as the
    // `d` hints; a long-press is rare enough that caching the boxes per page
    // would only hold that RAM while reading.
    const auto page = section->loadPage(section->currentPage);
    if (!page) {
      LOG_ERR("ERS", "Long-press: failed to load page %d", section->currentPage);
      return true;
    }
    // Same origin renderBook() draws the page at.
    int marginTop, marginRight, marginBottom, marginLeft;
    renderer.getOrientedViewableTRBL(&marginTop, &marginRight, &marginBottom, &marginLeft);
    marginTop += SETTINGS.screenMargin;
    marginLeft += SETTINGS.screenMargin;
    const int fontId = SETTINGS.getReaderFontId();
    std::vector<WordBox> words;
    deckpoint::reader::extractPageWords(renderer, *page, fontId, marginLeft, marginTop, words);
    const int hit = deckpoint::reader::findWordAt(words.data(), words.size(), x, y, renderer.getLineHeight(fontId),
                                                  WORD_TOUCH_SLOP_PX);
    if (hit < 0) {
      LOG_DBG("ERS", "Long-press at %d,%d: no word", x, y);
      return true;
    }

    snprintf(word, sizeof(word), "%s", words[hit].text);
    LOG_DBG("ERS", "Long-press lookup '%s' at %d,%d", word, x, y);
    settleOverlayRefresh();
    markPickedWordLocked(words[hit]);
  }
  lookUpPickedWord(word);
  return true;
}
