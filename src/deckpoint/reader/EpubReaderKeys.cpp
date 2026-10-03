// DECKPOINT: keyboard reader keys for EpubReaderActivity (vim-style bindings,
// marks, jump-back). Kept out of EpubReaderActivity.cpp so the upstream file
// only carries the hooks. The parser itself is ReaderKeys (host-tested).

#include <GfxRenderer.h>
#include <HalKeyboard.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <limits>

#include "CrossPointSettings.h"
#include "activities/ActivityManager.h"
#include "activities/reader/EpubReaderActivity.h"
#include "activities/reader/ReaderUtils.h"
#include "components/UITheme.h"
#include "deckpoint/KeyHelpActivity.h"

using deckpoint::reader::MarkPosition;
using deckpoint::reader::ReaderCmd;
using deckpoint::reader::ReaderCommand;

namespace {
// Same gap the button path enforces between single page turns.
constexpr unsigned long MIN_KEY_TURN_GAP_MS = 200;
}  // namespace

bool EpubReaderActivity::wantsRawKeys() const {
  if (!halKeyboard.present()) return false;
  // Anything that owns input through the button bridge keeps it: the load
  // failure dialog, the toolbar menu and its option picker, automatic page
  // turning (Enter / Esc stop it) and the end-of-book screen and menu.
  if (!epub || loadFailurePopup.isActive()) return false;
  if (overlay != Overlay::None || overlayPopup.isActive()) return false;
  if (automaticPageTurnActive) return false;
  if (isAtEndOfBook() || endOfBookMenuActive()) return false;
  return true;
}

void EpubReaderActivity::onKey(const freeink::KeyEvent& event) {
  // A key earlier in this batch opened a screen or overlay; the rest belong to it.
  if (keysSuspended || !wantsRawKeys()) return;
  if (hintsOpen) {
    hintKey(event);
    return;
  }
  if (cmdLine.isOpen()) {
    commandLineKey(event);
    return;
  }
  runReaderCommand(readerKeys.feed(event));
}

void EpubReaderActivity::runReaderCommand(const ReaderCommand& cmd) {
  if (cmd.type == ReaderCmd::Pending) {
    showKeyPopup(readerKeys.pendingText(), false);
    return;
  }

  // Whatever popup is up goes away: commands that re-render erase it anyway,
  // the rest get a clean repaint below.
  const bool hadPopup = keyPopupShown;
  keyPopupShown = false;
  bool leftReader = false;
  bool rendered = true;

  switch (cmd.type) {
    case ReaderCmd::NextPage:
    case ReaderCmd::PrevPage:
      keyPageTurns(cmd.type == ReaderCmd::NextPage, cmd.count);
      break;
    case ReaderCmd::BookStart:
      keyJumpToSpine(0, false);
      break;
    case ReaderCmd::BookEnd:
      keyJumpToSpine(epub->getSpineItemsCount() - 1, true);
      break;
    case ReaderCmd::NextChapter:
      keyChapterJump(cmd.count);
      break;
    case ReaderCmd::PrevChapter:
      keyChapterJump(-static_cast<int>(cmd.count));
      break;
    case ReaderCmd::GoPercent:
      jumpBackPosition = capturePosition();
      hasJumpBack = true;
      jumpToPercent(cmd.count);
      break;
    case ReaderCmd::Toc:
      chapterSelectOrigin = capturePosition();
      openChapterSelect(true);
      leftReader = true;
      break;
    case ReaderCmd::SetMark: {
      rendered = false;
      if (!ensureMarksLoaded()) break;
      marks->set(cmd.arg, capturePosition());
      if (!deckpoint::reader::saveMarks(epub->getCachePath(), *marks)) {
        LOG_ERR("ERS", "Failed to save mark %c", cmd.arg);
      }
      char msg[48];
      snprintf(msg, sizeof(msg), "%s: %c", tr(STR_KEYS_MARK_SET), cmd.arg);
      showKeyPopup(msg, true);
      break;
    }
    case ReaderCmd::JumpMark: {
      const MarkPosition* mark = ensureMarksLoaded() ? marks->get(cmd.arg) : nullptr;
      if (mark == nullptr) {
        rendered = false;
        char msg[48];
        snprintf(msg, sizeof(msg), "%s: %c", tr(STR_KEYS_NO_MARK), cmd.arg);
        showKeyPopup(msg, true);
        break;
      }
      const MarkPosition target = *mark;
      const MarkPosition origin = capturePosition();
      if (jumpToPosition(target)) {
        jumpBackPosition = origin;
        hasJumpBack = true;
      }
      break;
    }
    case ReaderCmd::JumpBack: {
      if (!hasJumpBack) {
        rendered = false;
        showKeyPopup(tr(STR_KEYS_NO_JUMP_BACK), true);
        break;
      }
      const MarkPosition target = jumpBackPosition;
      const MarkPosition origin = capturePosition();
      if (jumpToPosition(target)) jumpBackPosition = origin;
      break;
    }
    case ReaderCmd::ToggleBookmark:
      if (section && !showBookmarkMessage) {
        addBookmark();
        showBookmarkMessage = true;
        bookmarkMessageTime = millis();
        requestUpdate();
      }
      break;
    case ReaderCmd::Menu:
      pendingManualTurn = 0;
      if (usesToolbarMenu() && section) {
        openOverlay(Overlay::Toolbar);
      } else {
        openReaderMenu();
      }
      leftReader = true;
      break;
    case ReaderCmd::Back:
      // Same as a short Back press: unwind a followed link first, else leave.
      if (footnoteDepth > 0) {
        restoreSavedPosition();
        break;
      }
      if (SETTINGS.backShortToFileBrowser) {
        activityManager.goToFileBrowser(bookPath);
      } else {
        onGoHome();
      }
      leftReader = true;
      break;
    case ReaderCmd::Help:
      deckpoint::openKeyHelp(renderer, mappedInput, name.c_str(), keyHelp());
      leftReader = true;
      break;
    case ReaderCmd::CommandLine:
    case ReaderCmd::LookupWord:
      // A dropped prefix popup is still in the framebuffer: re-render under the line.
      openCommandLine(cmd.type == ReaderCmd::LookupWord ? "dict " : "", hadPopup);
      return;
    case ReaderCmd::Dictionary:
      // Like the `:` line: a dropped prefix popup means a clean render first.
      openHints(hadPopup);
      return;
    case ReaderCmd::Search:
    case ReaderCmd::SearchNext:
    case ReaderCmd::SearchPrev:
      rendered = false;
      showKeyPopup(tr(STR_KEYS_COMING_SOON), true);
      break;
    case ReaderCmd::Cancel:
    case ReaderCmd::None:
    default:
      rendered = false;
      break;
  }

  if (leftReader) {
    keysSuspended = true;
    return;
  }
  // A dropped prefix leaves its popup on the page; repaint once to clear it.
  if (hadPopup && !rendered && !keyPopupShown) requestUpdate();
}

void EpubReaderActivity::keyPageTurns(const bool forward, const int count) {
  if (!section) return;
  if (count <= 1) {
    // Single turns coalesce like button presses: one pending turn while a
    // render is in flight, replayed by loop().
    if (RenderLock::peek() || (millis() - lastPageTurnTime) < MIN_KEY_TURN_GAP_MS) {
      pendingManualTurn = forward ? 1 : -1;
      return;
    }
  }
  pendingManualTurn = 0;
  // Within a chapter each turn is a page-index step; a turn into the next
  // chapter carries the remaining count over as that chapter's start page.
  // Backward turns stop at the previous chapter's last page.
  bool turned = false;
  for (int i = 0; i < count; i++) {
    if (!pageTurn(forward)) break;
    turned = true;
    if (isAtEndOfBook()) break;
    if (!section) {
      const int remaining = count - i - 1;
      if (forward && remaining > 0) {
        RenderLock lock;
        nextPageNumber = remaining;
      }
      break;
    }
  }
  notePageTurn(forward, turned);
  requestUpdate();
}

void EpubReaderActivity::keyJumpToSpine(const int spineIndex, const bool lastPage) {
  if (!epub || spineIndex < 0 || spineIndex >= epub->getSpineItemsCount()) return;
  jumpBackPosition = capturePosition();
  hasJumpBack = true;
  {
    RenderLock lock;
    clearDeferredReposition();
    pendingManualTurn = 0;
    pendingAnchor.clear();
    pendingOffsetJump.reset();
    pendingPercentJump = false;
    if (section && currentSpineIndex == spineIndex && !(lastPage && section->isBuilding())) {
      section->currentPage = lastPage ? std::max(0, static_cast<int>(section->pageCount) - 1) : 0;
    } else {
      currentSpineIndex = spineIndex;
      nextPageNumber = 0;
      // Same sentinel a backward turn into a chapter uses: build it all, land on the last page.
      if (lastPage) {
        pendingPageJump = std::numeric_limits<uint16_t>::max();
      } else {
        pendingPageJump.reset();
      }
      section.reset();
    }
  }
  requestUpdate();
}

void EpubReaderActivity::keyChapterJump(const int delta) {
  if (!epub || delta == 0) return;
  const int spineCount = epub->getSpineItemsCount();
  if (spineCount <= 0) return;
  int target;
  if (delta > 0) {
    target = std::min(currentSpineIndex + delta, spineCount - 1);
    if (target == currentSpineIndex) return;  // already in the last chapter
  } else {
    // Like [[ in vim: the first step goes to the start of this chapter.
    const int page = section ? section->currentPage : nextPageNumber;
    const int steps = -delta - (page > 0 ? 1 : 0);
    target = std::max(0, currentSpineIndex - steps);
    if (target == currentSpineIndex && page <= 0) return;  // already at the book's first chapter start
  }
  keyJumpToSpine(target, false);
}

MarkPosition EpubReaderActivity::capturePosition() {
  MarkPosition pos;
  RenderLock lock;
  const int page = std::max(0, section ? section->currentPage : nextPageNumber);
  pos.spineIndex = static_cast<uint16_t>(std::max(0, currentSpineIndex));
  pos.page = static_cast<uint16_t>(std::min(page, static_cast<int>(std::numeric_limits<uint16_t>::max())));
  std::optional<uint32_t> offset;
  if (section && page < static_cast<int>(section->pageCount)) {
    offset = section->getVisibleTextOffsetForPage(static_cast<uint16_t>(page));
  } else if (!section && cachedSpineIndex == currentSpineIndex) {
    // A child screen released the section; this is the offset it was on.
    offset = cachedVisibleTextOffset;
  }
  if (offset.has_value()) {
    pos.hasVisibleTextOffset = true;
    pos.visibleTextOffset = *offset;
  }
  return pos;
}

bool EpubReaderActivity::jumpToPosition(const MarkPosition& position) {
  if (!epub || position.spineIndex >= epub->getSpineItemsCount()) return false;
  {
    // Same restore the KOReader sync / bookmark result path uses: map the
    // text offset when the chapter is already laid out that far, else rebuild
    // the chapter and let render() resolve the offset.
    RenderLock lock;
    clearDeferredReposition();
    pendingManualTurn = 0;
    pendingAnchor.clear();
    pendingPageJump.reset();
    pendingPercentJump = false;
    const bool sameSection = section && currentSpineIndex == position.spineIndex;
    if (position.hasVisibleTextOffset) {
      if (sameSection &&
          (!section->isBuilding() || section->buildReachedVisibleTextOffset(position.visibleTextOffset))) {
        const auto page = section->getPageForVisibleTextOffset(position.visibleTextOffset);
        section->currentPage = page.has_value() ? static_cast<int>(*page) : static_cast<int>(position.page);
      } else {
        currentSpineIndex = position.spineIndex;
        pendingOffsetJump = position.visibleTextOffset;
        nextPageNumber = position.page;
        section.reset();
      }
    } else if (sameSection && !section->isBuilding()) {
      section->currentPage = std::min(static_cast<int>(position.page), std::max(0, section->pageCount - 1));
    } else {
      currentSpineIndex = position.spineIndex;
      nextPageNumber = position.page;
      section.reset();
    }
  }
  requestUpdate();
  return true;
}

bool EpubReaderActivity::ensureMarksLoaded() {
  if (marks) return true;
  // ~320 B, only for readers that use marks; lives until the reader closes.
  marks = makeUniqueNoThrow<deckpoint::reader::MarkTable>();
  if (!marks) {
    LOG_ERR("ERS", "OOM: mark table");
    return false;
  }
  deckpoint::reader::loadMarks(epub->getCachePath(), *marks);
  return true;
}

void EpubReaderActivity::showKeyPopup(const char* text, const bool timed) {
  // Serialized against the render task.
  RenderLock lock;
  showKeyPopupLocked(text, timed);
}

void EpubReaderActivity::showKeyPopupLocked(const char* text, const bool timed) {
  // Painted straight over the page already in the framebuffer (one FAST
  // refresh, no page re-render).
  if (!section || !renderer.hasFrameBuffer()) return;
  GUI.drawPopup(renderer, text);
  keyPopupShown = true;
  keyPopupTime = timed ? std::max(1UL, millis()) : 0;
  keyPopupRenderStamp = lastRenderCompleteMs;
}

void EpubReaderActivity::keyPopupTick() {
  keysSuspended = false;
  if (!keyPopupShown) return;
  // A page render since the popup was painted already erased it.
  if (lastRenderCompleteMs != keyPopupRenderStamp) {
    keyPopupShown = false;
    return;
  }
  if (keyPopupTime != 0 && millis() - keyPopupTime >= ReaderUtils::BOOKMARK_MESSAGE_DURATION_MS) {
    keyPopupShown = false;
    requestUpdate();
  }
}
