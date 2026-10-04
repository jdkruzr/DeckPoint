// DECKPOINT: the reader's `d` hint-mode dictionary. Every word on the page
// gets a short label (HintMode, host-tested) drawn as a small inverted tag
// just above the word (hintTagRect), so its first letter stays visible;
// typing a label looks that word up through the `:dict` path.
//
// Refresh model (same as the `:` line): the clean page is copied out of the
// framebuffer once (storeBwBuffer, 9.6 KB on the T-Deck Pro) and the labels
// are pushed with a FAST refresh. Narrowing restores the copy and draws the
// remaining tags; cancelling restores it and pushes once. Without a copy
// (allocation failed, X4-class panels) the page is re-rendered instead.
//
// The session machinery (page + word boxes, stored page, stale / redraw
// handling) also carries the highlight selection (purpose Select,
// EpubReaderSelection.cpp); drawHintOverlay() picks what to draw.

#include <BoardConfig.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "activities/RenderLock.h"
#include "activities/reader/EpubReaderActivity.h"
#include "fontIds.h"

using deckpoint::CommandResult;
using deckpoint::reader::HintLabels;
using deckpoint::reader::HintMatcher;
using deckpoint::reader::HintPurpose;
using deckpoint::reader::HintSession;
using deckpoint::reader::WordBox;

namespace {

constexpr int LABEL_FONT_ID = SMALL_FONT_ID;
constexpr int LABEL_PAD = 1;
// Longest word copied out for the lookup (bytes, incl. NUL); the dictionary
// matches headwords far shorter than this.
constexpr size_t MAX_LOOKUP_WORD = 64;

// Same test as EpubReaderActivity.cpp / ReaderCommands.cpp: these panels
// re-render to restore the grayscale-AA planes, so a stored page is never used.
bool xteinkClassPanel() { return gpio.isXteinkDevice() || BoardConfig::isX4Pro() || BoardConfig::isX4Classic(); }

}  // namespace

void EpubReaderActivity::openHints(const bool pageDirty, const HintPurpose purpose) {
  readerKeys.reset();
  pendingManualTurn = 0;
  if (purpose == HintPurpose::Lookup && SETTINGS.dictionaryName[0] == '\0') {
    showKeyPopup(tr(STR_DICT_NO_DICT_SET), true);
    return;
  }
  if (purpose == HintPurpose::Select && (!annotationStore || annotationStore->readOnly())) {
    showKeyPopup(tr(STR_SEL_UNAVAILABLE), true);
    return;
  }
  if (!section) return;
  keyPopupShown = false;
  hintsOpen = true;
  hintsSelect = purpose == HintPurpose::Select;
  RenderLock lock;
  hintPendingPurpose = purpose;
  if (pageDirty || showBookmarkMessage || showDictionaryMessage) {
    // A popup is painted on the page: render a clean one first,
    // hintsAfterRender() puts the labels on it.
    showBookmarkMessage = false;
    showDictionaryMessage = false;
    hintOpenPending = true;
    requestUpdate();
    return;
  }
  settleOverlayRefresh();
  beginHints();
}

std::unique_ptr<HintSession> EpubReaderActivity::loadHintSessionLocked() {
  if (!section || !renderer.hasFrameBuffer()) return nullptr;
  // Page + word boxes (~2 KB for a full page) live until the overlay closes.
  auto session = makeUniqueNoThrow<HintSession>();
  if (!session) {
    LOG_ERR("HNT", "OOM: HintSession");
    showKeyPopupLocked(tr(STR_DICT_LOW_MEMORY), true);
    return nullptr;
  }
  session->page = section->loadPage(section->currentPage);
  if (!session->page) {
    LOG_ERR("HNT", "Failed to load page %d", section->currentPage);
    return nullptr;
  }
  // Same origin renderBook() draws the page at.
  int marginTop, marginRight, marginBottom, marginLeft;
  renderer.getOrientedViewableTRBL(&marginTop, &marginRight, &marginBottom, &marginLeft);
  marginTop += SETTINGS.screenMargin;
  marginLeft += SETTINGS.screenMargin;
  deckpoint::reader::extractPageWords(renderer, *session->page, SETTINGS.getReaderFontId(), marginLeft, marginTop,
                                      session->words);
  return session;
}

void EpubReaderActivity::beginHints() {
  hintOpenPending = false;
  auto session = loadHintSessionLocked();
  if (!session) {
    pendingNoteHighlight = -1;
    return;
  }
  if (pendingNoteHighlight >= 0) {
    // `e` in the notes list: straight to the note sheet on this page (if it
    // is not there, hintsTick() closes the empty session).
    openPendingNoteLocked(std::move(session));
    return;
  }
  if (session->words.empty()) {
    showKeyPopupLocked(tr(STR_KEYS_NO_WORDS), true);
    return;
  }
  session->purpose = hintPendingPurpose;
  if (session->purpose == HintPurpose::Select) session->selection.begin();
  session->matcher.begin(static_cast<uint16_t>(std::min<size_t>(session->words.size(), HintLabels::MAX_TARGETS)));
  session->pageStored = storeHintPageLocked();
  hints = std::move(session);
  drawHintOverlay();
  pushOverlayRefresh();
}

bool EpubReaderActivity::storeHintPageLocked() { return !xteinkClassPanel() && renderer.storeBwBuffer(); }

void EpubReaderActivity::drawHintOverlay() {
  if (!hints) return;
  if (hints->purpose == HintPurpose::Select) {
    drawSelectionOverlay();
  } else {
    drawHintLabels();
  }
}

bool EpubReaderActivity::repaintHintsLocked() {
  if (!hints) return false;
  if (!hints->pageStored) {
    hints->redrawAfterRender = true;
    return true;
  }
  settleOverlayRefresh();
  renderer.restoreBwBuffer(/*resyncPanelBaseline=*/false);
  // The restore freed the copy; take a fresh one for the next change.
  hints->pageStored = renderer.storeBwBuffer();
  drawHintOverlay();
  pushOverlayRefresh();
  return false;
}

void EpubReaderActivity::drawHintLabels() const {
  if (!hints) return;
  const HintMatcher& matcher = hints->matcher;
  const size_t typedLen = strlen(matcher.typed());
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int labelAscender = renderer.getFontAscenderSize(LABEL_FONT_ID);
  const int readerAscender = renderer.getFontAscenderSize(SETTINGS.getReaderFontId());
  const int tagH = deckpoint::reader::hintLabelCapHeight(labelAscender) + 2 * LABEL_PAD;
  for (uint16_t i = 0; i < matcher.labels().count(); i++) {
    if (!matcher.matches(i)) continue;
    char label[HintLabels::MAX_LABEL_LEN + 1];
    matcher.labels().label(i, label);
    // Once a prefix is typed only the letter still to type is shown, in
    // capitals (typing is case-blind).
    char shown[HintLabels::MAX_LABEL_LEN + 1];
    size_t n = 0;
    for (const char* c = label + typedLen; *c && n < HintLabels::MAX_LABEL_LEN; c++) {
      shown[n++] = (*c >= 'a' && *c <= 'z') ? static_cast<char>(*c - 'a' + 'A') : *c;
    }
    shown[n] = '\0';
    const WordBox& word = hints->words[i];
    const int tagW = renderer.getTextWidth(LABEL_FONT_ID, shown) + 2 * LABEL_PAD;
    const auto tag = deckpoint::reader::hintTagRect(word.x, word.y, readerAscender, tagW, tagH, screenW, screenH);
    renderer.fillRect(tag.x, tag.y, tag.width, tag.height, true);
    // drawText's y is the line top (baseline = y + ascender): sit the
    // baseline on the tag's bottom padding.
    renderer.drawText(LABEL_FONT_ID, tag.x + LABEL_PAD, tag.y + tag.height - LABEL_PAD - labelAscender, shown, false);
  }
}

bool EpubReaderActivity::closeHintsLocked(const bool restorePage) {
  hintsOpen = false;
  hintsSelect = false;
  hintOpenPending = false;
  pendingNoteHighlight = -1;
  if (!hints) return false;
  bool rerender = false;
  if (restorePage && hints->pageStored && !hints->stale && !pageHasGray) {
    settleOverlayRefresh();
    // No baseline resync: the glass shows the labels, and erasing them needs
    // the differential to keep diffing against the last pushed frame.
    renderer.restoreBwBuffer(/*resyncPanelBaseline=*/false);
    pushOverlayRefresh();
  } else {
    if (hints->pageStored) renderer.discardStoredBwBuffer();
    rerender = restorePage && !hints->stale;
  }
  hints.reset();
  return rerender;
}

void EpubReaderActivity::closeHints(const bool restorePage) {
  bool rerender;
  {
    RenderLock lock;
    rerender = closeHintsLocked(restorePage);
  }
  if (rerender) requestUpdate();
}

void EpubReaderActivity::hintKey(const freeink::KeyEvent& event) {
  char word[MAX_LOOKUP_WORD];
  word[0] = '\0';
  bool rerender = false;
  bool passThrough = false;
  {
    RenderLock lock;
    if (!hints || hints->stale) {
      const bool cancel =
          event.special == freeink::SpecialKey::Escape || event.special == freeink::SpecialKey::Backspace;
      if (hintOpenPending && !cancel) return;  // labels not up yet: wait for them
      // Esc dropped a pending open, or a page render already erased the
      // labels and the key belongs to the reader again.
      passThrough = !hintOpenPending;
      closeHintsLocked(false);
    } else {
      switch (hints->matcher.feed(event)) {
        case HintMatcher::Result::Narrowed:
          rerender = repaintHintsLocked();
          break;
        case HintMatcher::Result::Cancelled:
          rerender = closeHintsLocked(true);
          break;
        case HintMatcher::Result::Selected: {
          const WordBox& box = hints->words[hints->matcher.selected()];
          snprintf(word, sizeof(word), "%s", box.text);
          settleOverlayRefresh();
          if (hints->pageStored) renderer.restoreBwBuffer(/*resyncPanelBaseline=*/false);
          hints->pageStored = false;
          markPickedWordLocked(box);
          hints.reset();
          hintsOpen = false;
          hintsSelect = false;
          break;
        }
        case HintMatcher::Result::Ignored:
        default:
          break;
      }
    }
  }
  if (rerender) requestUpdate();
  if (passThrough) {
    runReaderCommand(readerKeys.feed(event));
    return;
  }
  if (word[0] == '\0') return;
  lookUpPickedWord(word);
}

void EpubReaderActivity::markPickedWordLocked(const WordBox& box) {
  // The chosen word inverted on the clean page; the "Looking up" popup that
  // follows pushes both in one refresh.
  const int fontId = SETTINGS.getReaderFontId();
  if (auto* fontCache = renderer.getFontCacheManager()) {
    fontCache->prewarmCache(fontId, box.text, static_cast<uint8_t>(1u << (static_cast<uint8_t>(box.style) & 0x03)));
  }
  renderer.fillRect(box.x - 2, box.y - 2, box.width + 4, renderer.getLineHeight(fontId) + 4, true);
  renderer.drawText(fontId, box.x, box.y, box.text, false, box.style);
}

void EpubReaderActivity::lookUpPickedWord(const char* word) {
  char msg[96];
  msg[0] = '\0';
  switch (lookUpWord(word, msg, sizeof(msg), true)) {
    case CommandResult::Left:
      keysSuspended = true;
      break;
    case CommandResult::Message:
      // The toast's expiry re-renders the page, clearing the inverted word.
      showKeyPopup(msg, true);
      break;
    default:
      requestUpdate();
      break;
  }
}

bool EpubReaderActivity::hintsTick() {
  if (!hintsOpen) return false;
  // A touch-opened selection also runs without a keyboard.
  if (hintsSelect ? !readerOwnsPage() : !wantsRawKeys()) {
    // Something else took over the screen: drop the labels without painting.
    closeHints(false);
    return false;
  }
  {
    // A render that dropped the labels (or a pending open that found no
    // words) ends the hints; never block the loop on a render for this.
    RenderLock lock(RenderLock::Mode::Try);
    if (lock.ownsLock() && !hintOpenPending && (!hints || hints->stale)) {
      closeHintsLocked(false);
      return false;
    }
  }
  noteSheetTick();
  // Hold the auto-turn interval and swallow buttons / taps while picking.
  lastPageTurnTime = millis();
  return true;
}

void EpubReaderActivity::hintsBeforeRender() {
  if (!hints) return;
  if (hints->pageStored) {
    renderer.discardStoredBwBuffer();
    hints->pageStored = false;
  }
  if (!hints->redrawAfterRender) hints->stale = true;
}

void EpubReaderActivity::hintsAfterRender() {
  if (pendingHintToast != nullptr) {
    // A selection ended with a message and a page render (saved / deleted
    // highlight, or a gray page re-rendered): the message goes on top of it.
    const char* toast = pendingHintToast;
    pendingHintToast = nullptr;
    showKeyPopupLocked(toast, true);
  }
  if (hintOpenPending) {
    beginHints();
    return;
  }
  if (!hints || hints->stale || !hints->redrawAfterRender || !renderer.hasFrameBuffer()) return;
  hints->redrawAfterRender = false;
  hints->pageStored = storeHintPageLocked();
  drawHintOverlay();
  pushOverlayRefresh();
}
