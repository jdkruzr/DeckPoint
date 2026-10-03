// DECKPOINT: the reader's `/` search with n / N.
//
// The search streams the book page by page through Section::loadPage, feeding
// each word to PhraseMatcher (SearchMatch, host-tested): no chapter text is
// ever held in RAM, only one Page at a time. It runs in ~40 ms slices from
// loop() so the keyboard stays live (Esc stops it) and the band over the
// status bar can show which chapter it is in.
//
// Forward: the open chapter from the current page, the chapters after it,
// then from the start of the book back to the start page. Chapters without a
// layout cache for the current settings are laid out with the reader's own
// incremental build (Section::startBuild / buildSomeMore, behind the same heap
// gate as background builds) and stay cached for later. Phrases may span
// lines and, going forward, pages. Backward (N) scans each page on its own
// and keeps the page's last match; a chapter is laid out fully before its
// last page is known.
//
// A hit records the jump-back position (''), jumps there and inverts the
// matched words over the rendered page (PageWords boxes). Like the hint
// labels, the clean page is kept with storeBwBuffer so the next key puts it
// back with one FAST refresh.

#include <BoardConfig.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <vector>

#include "CrossPointSettings.h"
#include "activities/RenderLock.h"
#include "activities/reader/EpubReaderActivity.h"
#include "deckpoint/reader/PageWords.h"

using deckpoint::CommandResult;
using deckpoint::reader::endsWithHyphenBreak;
using deckpoint::reader::MarkPosition;
using deckpoint::reader::SearchSession;
using deckpoint::reader::WordBox;

namespace {

// Work per loop() pass before the keyboard is polled again.
constexpr unsigned long SLICE_MS = 40;
// The band shows progress only once a search has run this long (most hits on
// nearby pages land before it), then at most once per chapter / repaint gap.
constexpr unsigned long PROGRESS_DELAY_MS = 400;
constexpr unsigned long PROGRESS_REPAINT_MS = 1500;
constexpr int BUILD_PAGES_PER_STEP = 2;
// Slices in a row the build heap gate may refuse before the search gives up.
constexpr uint16_t MAX_HEAP_WAITS = 150;
constexpr uint16_t MARK_TO_PAGE_END = 0xFFFF;

// Same test as EpubReaderActivity.cpp / ReaderCommands.cpp: these panels
// re-render to restore the grayscale-AA planes, so a stored page is never used.
bool xteinkClassPanel() { return gpio.isXteinkDevice() || BoardConfig::isX4Pro() || BoardConfig::isX4Classic(); }

void resetChapterScan(SearchSession& s) {
  s.matcher.reset();
  s.hyphenCarry = false;
  s.prevSpine = -1;
  s.prevPage = -1;
}

bool atOrigin(const SearchSession& s, const int spine, const int page) {
  return spine == s.originSpine && page == s.originPage;
}

bool acceptForward(const SearchSession& s, const int spine, const int page, const int32_t first) {
  if (!atOrigin(s, spine, page)) return true;
  return s.wrapped ? first <= s.originToken : first > s.originToken;
}

bool acceptBackward(const SearchSession& s, const int32_t first) {
  if (!atOrigin(s, s.spine, s.page)) return true;
  return s.wrapped ? first >= s.originToken : first < s.originToken;
}

}  // namespace

// ---------------------------------------------------------------------------
// Starting
// ---------------------------------------------------------------------------

void EpubReaderActivity::openSearchPrompt(const bool pageDirty) { openCommandLine("", pageDirty, true); }

bool EpubReaderActivity::startSearch(const char* query, const bool forward, const bool fromPrompt, char* msg,
                                     const size_t msgSize) {
  const char* text = query ? query : "";
  while (*text == ' ') ++text;
  if (*text == '\0') text = lastSearch;  // like vim: an empty search repeats the last one
  if (*text == '\0') {
    snprintf(msg, msgSize, "%s", tr(STR_SEARCH_NO_PREVIOUS));
    return false;
  }
  if (!epub || !section || buildViewportWidth == 0) {
    snprintf(msg, msgSize, "%s", tr(STR_LOADING));
    return false;
  }
  // ~1.1 KB (query units + match ring), only while the search runs.
  auto session = makeUniqueNoThrow<SearchSession>();
  if (!session) {
    LOG_ERR("SRC", "OOM: SearchSession");
    snprintf(msg, msgSize, "%s", tr(STR_DICT_LOW_MEMORY));
    return false;
  }
  if (!session->query.set(text)) {
    snprintf(msg, msgSize, "%s: %s", tr(STR_SEARCH_NOT_FOUND), text);
    return false;
  }
  if (text != lastSearch) snprintf(lastSearch, sizeof(lastSearch), "%s", text);

  SearchSession& s = *session;
  s.matcher.begin(&s.query);
  s.forward = forward;
  s.fromPrompt = fromPrompt;
  {
    RenderLock lock;
    s.originSpine = currentSpineIndex;
    s.originPage = section ? section->currentPage : 0;
    // n / N continue from the last hit while it is on this page; `/` and a
    // page without one cover the whole page.
    const bool fromHit = !fromPrompt && searchMark.spine == s.originSpine && searchMark.page == s.originPage;
    if (fromHit) {
      s.originToken = searchMark.firstToken;
    } else {
      s.originToken = forward ? -1 : SearchSession::NO_LIMIT;
    }
  }
  s.spine = s.originSpine;
  s.page = s.originPage;
  s.startMs = millis();
  LOG_DBG("SRC", "Search %s \"%s\" from ch %d p %d", forward ? "forward" : "backward", lastSearch, s.spine, s.page);
  search = std::move(session);
  return true;
}

void EpubReaderActivity::searchNextKey(const bool forward) {
  char msg[96];
  msg[0] = '\0';
  if (!startSearch(lastSearch, forward, false, msg, sizeof(msg))) showKeyPopup(msg, true);
}

bool EpubReaderActivity::searchKey(const freeink::KeyEvent& event) {
  if (!search) return false;
  if (event.special == freeink::SpecialKey::Escape || event.special == freeink::SpecialKey::Backspace) {
    finishSearch(SearchStep::Cancelled, nullptr);
  }
  return true;  // everything else waits for the search
}

// ---------------------------------------------------------------------------
// Running
// ---------------------------------------------------------------------------

bool EpubReaderActivity::searchTick() {
  if (!search) return false;
  if (!wantsRawKeys()) {
    // Something else took the screen: drop the search without painting.
    {
      RenderLock lock;
      search.reset();
    }
    if (cmdLine.isOpen()) closeCommandLine(CommandResult::Left);
    return false;
  }
  // Hold the auto-turn interval while searching.
  lastPageTurnTime = millis();

  char msg[96];
  msg[0] = '\0';
  SearchStep result = SearchStep::Continue;
  {
    RenderLock lock(RenderLock::Mode::Try);
    if (!lock.ownsLock()) return true;
    const unsigned long start = millis();
    do {
      result = searchStep(msg, sizeof(msg));
    } while (result == SearchStep::Continue && millis() - start < SLICE_MS);
  }
  if (result == SearchStep::Continue || result == SearchStep::Yield) {
    paintSearchProgress();
    return true;
  }
  finishSearch(result, msg);
  return true;
}

// One unit of work: scan a page, lay out a few pages, or move to the next
// chapter. Caller holds the RenderLock (the builds measure text with the
// renderer, and the open chapter's Section is shared with the render task).
EpubReaderActivity::SearchStep EpubReaderActivity::searchStep(char* msg, const size_t msgSize) {
  SearchSession& s = *search;
  const int spineCount = epub->getSpineItemsCount();
  if (spineCount <= 0) return SearchStep::NotFound;

  if (s.forward && s.spine >= spineCount) {
    s.spine = 0;
    s.page = 0;
    s.wrapped = true;
    resetChapterScan(s);
  } else if (!s.forward && s.spine < 0) {
    s.spine = spineCount - 1;
    s.page = SearchSession::LAST_PAGE;
    s.wrapped = true;
    resetChapterScan(s);
  }

  // Back around to the start page: the part of it not yet searched decides.
  if (s.wrapped) {
    if (s.forward) {
      if (s.spine > s.originSpine || (s.spine == s.originSpine && s.page > s.originPage)) return SearchStep::NotFound;
      if (atOrigin(s, s.spine, s.page) && s.originToken < 0) return SearchStep::NotFound;
    } else if (s.spine < s.originSpine) {
      return SearchStep::NotFound;
    } else if (s.page != SearchSession::LAST_PAGE && s.spine == s.originSpine) {
      if (s.page < s.originPage) return SearchStep::NotFound;
      if (s.page == s.originPage && s.originToken == SearchSession::NO_LIMIT) return SearchStep::NotFound;
    }
  }

  const ReaderRenderSpec spec = SETTINGS.readerRenderSpec(buildViewportWidth, buildViewportHeight);
  Section* sec;
  bool complete;
  if (s.spine == currentSpineIndex && section) {
    s.section.reset();
    s.sectionSpine = -1;
    sec = section.get();
    complete = !sec->isBuilding() && !sec->isPartial();
  } else {
    if (s.sectionSpine != s.spine) {
      s.section.reset();  // finished (or never built) by now: nothing to persist
      s.sectionSpine = -1;
      auto fresh = makeUniqueNoThrow<Section>(epub, s.spine, renderer);
      if (!fresh) {
        LOG_ERR("SRC", "OOM: Section %d", s.spine);
        snprintf(msg, msgSize, "%s", tr(STR_DICT_LOW_MEMORY));
        return SearchStep::Failed;
      }
      s.cacheLoaded = fresh->loadSectionFile(spec) && !fresh->isPartial();
      s.section = std::move(fresh);
      s.sectionSpine = s.spine;
    }
    sec = s.section.get();
    complete = s.cacheLoaded || sec->isBuildComplete();
  }

  const bool needLayout = s.page == SearchSession::LAST_PAGE ? !complete : s.page >= static_cast<int>(sec->pageCount);
  if (needLayout && complete) {
    // Forward past the chapter's last page.
    s.spine++;
    s.page = 0;
    resetChapterScan(s);
    return SearchStep::Continue;
  }
  if (needLayout) {
    if (!buildTickHeapGate()) {
      if (++s.heapWaits > MAX_HEAP_WAITS) {
        LOG_ERR("SRC", "Heap stayed low for the chapter %d build", s.spine);
        snprintf(msg, msgSize, "%s", tr(STR_DICT_LOW_MEMORY));
        return SearchStep::Failed;
      }
      return SearchStep::Yield;
    }
    s.heapWaits = 0;
    if (!sec->isBuilding()) {
      // One layout build at a time (they share the book's CSS rules): the
      // open chapter keeps the pages it has as a partial and resumes later.
      if (sec != section.get() && section && section->isBuilding()) section->suspendBuild();
      LOG_DBG("SRC", "Laying out chapter %d for search", s.spine);
      if (!sec->startBuild(spec)) {
        LOG_ERR("SRC", "Failed to start the chapter %d build", s.spine);
        snprintf(msg, msgSize, "%s", tr(STR_SEARCH_FAILED));
        return SearchStep::Failed;
      }
      return SearchStep::Yield;  // inflating a big chapter can take a while: poll keys
    }
    if (!sec->buildSomeMore(BUILD_PAGES_PER_STEP)) {
      LOG_ERR("SRC", "Chapter %d build failed", s.spine);
      if (sec == section.get()) {
        // Same recovery as the background build tick.
        section.reset();
        requestUpdate();
      } else {
        s.section.reset();
        s.sectionSpine = -1;
      }
      snprintf(msg, msgSize, "%s", tr(STR_SEARCH_FAILED));
      return SearchStep::Failed;
    }
    return SearchStep::Continue;
  }

  if (s.page == SearchSession::LAST_PAGE) {
    s.page = static_cast<int>(sec->pageCount) - 1;
    if (s.page < 0) {  // empty chapter
      s.spine--;
      s.page = SearchSession::LAST_PAGE;
    }
    return SearchStep::Continue;  // re-check the wrap end with the real page
  }

  const auto page = sec->loadPage(s.page);
  if (!page) {
    LOG_ERR("SRC", "Failed to load ch %d page %d", s.spine, s.page);
    snprintf(msg, msgSize, "%s", tr(STR_SEARCH_FAILED));
    return SearchStep::Failed;
  }

  if (!s.forward) {
    s.matcher.reset();
    s.hyphenCarry = false;
  }
  const bool prevValid = s.forward && s.prevSpine == s.spine && s.prevPage == s.page - 1;
  s.prevFirstToken = s.pageFirstToken;
  s.pageFirstToken = s.nextToken;

  bool found = false;
  int foundPage = s.page;
  uint16_t foundFirst = 0;
  uint16_t foundLast = 0;
  uint16_t local = 0;
  for (const auto& element : page->elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto* block = static_cast<const PageLine*>(element.get())->getBlock();
    // Same words, same order as extractPageWords' token numbering.
    if (!block || !block->valid()) continue;
    const uint16_t count = block->wordCount();
    for (uint16_t i = 0; i < count; i++, local++, s.nextToken++) {
      const char* text = block->wordText(i);
      const uint16_t len = block->wordTextLen(i);
      const bool matched = s.matcher.feed(text, len, s.nextToken, s.hyphenCarry);
      s.hyphenCarry = i + 1 == count && endsWithHyphenBreak(text, len);
      if (!matched) continue;
      const auto hit = s.matcher.hit();
      if (s.forward) {
        int hitPage = s.page;
        uint16_t first;
        uint16_t last = local;
        if (hit.firstToken >= s.pageFirstToken) {
          first = static_cast<uint16_t>(hit.firstToken - s.pageFirstToken);
        } else if (prevValid && hit.firstToken >= s.prevFirstToken) {
          // Began on the previous page: mark it from there to that page's end.
          hitPage = s.prevPage;
          first = static_cast<uint16_t>(hit.firstToken - s.prevFirstToken);
          last = MARK_TO_PAGE_END;
        } else {
          continue;
        }
        if (!acceptForward(s, s.spine, hitPage, first)) continue;
        found = true;
        foundPage = hitPage;
        foundFirst = first;
        foundLast = last;
        break;
      }
      if (hit.firstToken < s.pageFirstToken) continue;
      const auto first = static_cast<uint16_t>(hit.firstToken - s.pageFirstToken);
      if (!acceptBackward(s, first)) continue;
      found = true;  // keep going: the page's last match wins
      foundFirst = first;
      foundLast = local;
    }
    if (found && s.forward) break;
  }

  if (found) {
    searchMark.active = false;
    searchMark.spine = s.spine;
    searchMark.page = foundPage;
    searchMark.firstToken = foundFirst;
    searchMark.lastToken = foundLast;
    searchMark.wrappedToast = s.wrapped;
    return SearchStep::Hit;
  }

  s.prevSpine = s.spine;
  s.prevPage = s.page;
  if (s.forward) {
    s.page++;
  } else if (--s.page < 0) {
    s.spine--;
    s.page = SearchSession::LAST_PAGE;
  }
  return SearchStep::Continue;
}

void EpubReaderActivity::paintSearchProgress() {
  if (!search) return;
  SearchSession& s = *search;
  const unsigned long now = millis();
  if (now - s.startMs < PROGRESS_DELAY_MS) return;
  if (s.paintedSpine >= 0 && (s.paintedSpine == s.spine || now - s.lastPaintMs < PROGRESS_REPAINT_MS)) return;
  const int spineCount = epub->getSpineItemsCount();
  const int shown = std::clamp(s.spine, 0, std::max(0, spineCount - 1)) + 1;
  char text[64];
  snprintf(text, sizeof(text), "%s %d/%d...", tr(STR_SEARCH_PROGRESS), shown, spineCount);
  s.paintedSpine = s.spine;
  s.lastPaintMs = now;
  if (cmdLine.isOpen()) {
    snprintf(cmdMessage, sizeof(cmdMessage), "%s", text);
    cmdMessageTime = 0;  // stays until the search ends
    paintCommandLine();
  } else {
    // n / N: the band comes up for long searches only.
    openCommandLine(lastSearch, false, true, text);
  }
}

// ---------------------------------------------------------------------------
// Ending
// ---------------------------------------------------------------------------

void EpubReaderActivity::finishSearch(const SearchStep how, const char* msg) {
  std::unique_ptr<SearchSession> done;
  {
    // A search Section mid-build persists as a partial on the way out.
    RenderLock lock;
    done = std::move(search);
    if (done) done->section.reset();
  }
  if (!done) return;
  LOG_DBG("SRC", "Search ended (%u) after %lums", static_cast<unsigned>(how), millis() - done->startMs);

  if (how == SearchStep::Cancelled) {
    if (cmdLine.isOpen()) closeCommandLine(CommandResult::Restore);
    return;
  }

  if (how != SearchStep::Hit) {
    char text[96];
    if (how == SearchStep::NotFound || !msg || msg[0] == '\0') {
      snprintf(text, sizeof(text), "%s: %s", tr(STR_SEARCH_NOT_FOUND), lastSearch);
    } else {
      snprintf(text, sizeof(text), "%s", msg);
    }
    if (done->fromPrompt) {
      // The query stays on the line for an edit and another try.
      showCommandMessage(text);
      return;
    }
    if (cmdLine.isOpen()) closeCommandLine(CommandResult::Restore);
    RenderLock lock;
    settleOverlayRefresh();
    showKeyPopupLocked(text, true);
    return;
  }

  const MarkPosition origin = capturePosition();
  bool samePage;
  {
    RenderLock lock;
    samePage = section && searchMark.spine == currentSpineIndex && searchMark.page == section->currentPage;
  }
  if (!samePage) {
    jumpBackPosition = origin;
    hasJumpBack = true;
  }

  if (samePage) {
    bool clean = !keyPopupShown;
    {
      RenderLock lock;
      if (cmdLine.isOpen()) {
        // Put the page back under the band without pushing it; the mark
        // goes on top and both reach the glass in one refresh.
        cmdLine.close();
        cmdMessage[0] = '\0';
        cmdMessageTime = 0;
        cmdLineShown = false;
        settleOverlayRefresh();
        if (cmdPageStored) {
          renderer.restoreBwBuffer(/*resyncPanelBaseline=*/false);
          cmdPageStored = false;
        } else {
          clean = false;
        }
      }
      searchMark.active = true;
      if (clean) {
        settleOverlayRefresh();
        clean = drawSearchMarkLocked();
      }
    }
    if (!clean) requestUpdate();  // the render puts the mark on a fresh page
    return;
  }

  if (cmdLine.isOpen()) closeCommandLine(CommandResult::Left);
  {
    RenderLock lock;
    clearDeferredReposition();
    pendingManualTurn = 0;
    pendingAnchor.clear();
    pendingOffsetJump.reset();
    pendingPercentJump = false;
    if (section && currentSpineIndex == searchMark.spine && searchMark.page < static_cast<int>(section->pageCount)) {
      section->currentPage = searchMark.page;
    } else {
      currentSpineIndex = searchMark.spine;
      nextPageNumber = 0;
      pendingPageJump = static_cast<uint16_t>(searchMark.page);
      section.reset();
    }
    searchMark.active = true;
  }
  requestUpdate();
}

// ---------------------------------------------------------------------------
// The inverted hit
// ---------------------------------------------------------------------------

bool EpubReaderActivity::drawSearchMarkLocked() {
  if (!searchMark.active || !section || !renderer.hasFrameBuffer()) return false;
  if (searchMark.spine != currentSpineIndex || searchMark.page != section->currentPage || cmdLineShown || hints ||
      overlay != Overlay::None) {
    searchMark.active = false;
    return false;
  }
  const auto page = section->loadPage(section->currentPage);
  if (!page) return false;
  // Same origin renderBook() draws the page at.
  int marginTop, marginRight, marginBottom, marginLeft;
  renderer.getOrientedViewableTRBL(&marginTop, &marginRight, &marginBottom, &marginLeft);
  marginTop += SETTINGS.screenMargin;
  marginLeft += SETTINGS.screenMargin;
  const int fontId = SETTINGS.getReaderFontId();
  // Word boxes for the page (~2 KB for a full one), only while drawing.
  std::vector<WordBox> words;
  deckpoint::reader::extractPageWords(renderer, *page, fontId, marginLeft, marginTop, words);

  if (!searchMark.pageStored) searchMark.pageStored = !xteinkClassPanel() && renderer.storeBwBuffer();
  const int lineH = renderer.getLineHeight(fontId);
  const auto marked = [this](const WordBox& w) {
    return w.token >= searchMark.firstToken && w.token <= searchMark.lastToken;
  };
  // One bar per line across the marked words (covering the gaps between
  // them), then the words in white on it.
  for (size_t i = 0; i < words.size();) {
    if (!marked(words[i])) {
      i++;
      continue;
    }
    const uint16_t row = words[i].row;
    int left = words[i].x;
    int right = words[i].x + words[i].width;
    size_t j = i + 1;
    for (; j < words.size() && words[j].row == row && marked(words[j]); j++) {
      left = std::min(left, static_cast<int>(words[j].x));
      right = std::max(right, words[j].x + words[j].width);
    }
    renderer.fillRect(left - 2, words[i].y - 2, right - left + 4, lineH + 4, true);
    i = j;
  }
  auto* fontCache = renderer.getFontCacheManager();
  for (const WordBox& w : words) {
    if (!marked(w)) continue;
    if (fontCache) {
      fontCache->prewarmCache(fontId, w.text, static_cast<uint8_t>(1u << (static_cast<uint8_t>(w.style) & 0x03)));
    }
    renderer.drawText(fontId, w.x, w.y, w.text, false, w.style);
  }

  if (searchMark.wrappedToast) {
    searchMark.wrappedToast = false;
    showKeyPopupLocked(tr(STR_SEARCH_WRAPPED), true);  // pushes the mark with it
  } else {
    pushOverlayRefresh();
  }
  return true;
}

void EpubReaderActivity::clearSearchMark(const bool restore) {
  bool rerender = false;
  {
    RenderLock lock;
    if (!searchMark.active) return;
    searchMark.active = false;
    searchMark.wrappedToast = false;
    if (searchMark.pageStored) {
      if (restore) {
        settleOverlayRefresh();
        // No baseline resync: the glass shows the mark, and erasing it needs
        // the differential to keep diffing against the last pushed frame.
        renderer.restoreBwBuffer(/*resyncPanelBaseline=*/false);
        pushOverlayRefresh();
        keyPopupShown = false;  // a wrapped toast went with it
      } else {
        renderer.discardStoredBwBuffer();
      }
      searchMark.pageStored = false;
    } else {
      rerender = restore;
    }
  }
  if (rerender) requestUpdate();
}

void EpubReaderActivity::searchBeforeRender() {
  if (!searchMark.pageStored) return;
  renderer.discardStoredBwBuffer();
  searchMark.pageStored = false;
}

void EpubReaderActivity::searchAfterRender() {
  if (searchMark.active) drawSearchMarkLocked();
}
