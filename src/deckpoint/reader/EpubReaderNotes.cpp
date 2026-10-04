// DECKPOINT: the reader's side of the notes list (`:notes`, menu "Notes") and
// `:export`. The list (NotesListActivity) works on the reader's own
// AnnotationStore and hands back the picked entry; the reader jumps to the
// page holding its start (the underline marks it there) and, for `e`, opens
// the note sheet on that page once it is drawn (beginHints()).

#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include "activities/RenderLock.h"
#include "activities/reader/EpubReaderActivity.h"
#include "deckpoint/reader/NotesExport.h"
#include "deckpoint/reader/NotesListActivity.h"

using deckpoint::annotations::Annotation;
using deckpoint::annotations::Placement;
using deckpoint::reader::HintPurpose;
using deckpoint::reader::MarkPosition;

bool EpubReaderActivity::openNotesList(const bool fromKeys) {
  if (!annotationStore || !epub) return false;
  const MarkPosition origin = capturePosition();
  auto list = makeUniqueNoThrow<NotesListActivity>(renderer, mappedInput, epub, *annotationStore);
  if (!list) {
    LOG_ERR("NTL", "OOM: NotesListActivity");
    return false;
  }
  startActivityForResult(std::move(list), [this, fromKeys, origin](const ActivityResult& result) {
    if (result.isCancelled || !std::holds_alternative<AnnotationResult>(result.data)) {
      // A delete in the list may have taken an underline off this page.
      if (fromKeys) {
        requestUpdate();
      } else {
        openReaderMenu();
      }
      return;
    }
    const auto& pick = std::get<AnnotationResult>(result.data);
    jumpToAnnotation(pick.index, pick.editNote, origin);
  });
  return true;
}

void EpubReaderActivity::jumpToAnnotation(const int index, const bool editNote, const MarkPosition& origin) {
  if (!annotationStore || !epub || index < 0 || static_cast<size_t>(index) >= annotationStore->annotations().size()) {
    requestUpdate();
    return;
  }
  MarkPosition target;
  bool placed;
  {
    // Placement writes the entries the render task reads.
    RenderLock lock;
    const Annotation& a = annotationStore->annotations()[static_cast<size_t>(index)];
    if (a.spineIndex < 0 || a.spineIndex >= epub->getSpineItemsCount()) {
      LOG_ERR("NTL", "Annotation %d has no spine position", index);
      requestUpdate();
      return;
    }
    if (a.placement == Placement::Unresolved) annotationStore->placeChapter(epub, a.spineIndex);
    placed = a.placement == Placement::Resolved;
    target.spineIndex = static_cast<uint16_t>(a.spineIndex);
    target.hasVisibleTextOffset = placed;
    target.visibleTextOffset = placed ? a.startOffset : 0;  // unresolved: the chapter's first page
  }
  if (jumpToPosition(target)) {
    jumpBackPosition = origin;
    hasJumpBack = true;
  }
  if (!editNote || !placed || annotationStore->readOnly()) return;
  // Same path as `v`: the session opens after the page render, then switches
  // straight to the note sheet for this highlight.
  readerKeys.reset();
  keyPopupShown = false;
  hintsOpen = true;
  hintsSelect = true;
  RenderLock lock;
  hintPendingPurpose = HintPurpose::Select;
  pendingNoteHighlight = index;
  hintOpenPending = true;
}

bool EpubReaderActivity::openPendingNoteLocked(std::unique_ptr<deckpoint::reader::HintSession> session) {
  const int index = pendingNoteHighlight;
  pendingNoteHighlight = -1;
  const auto* list = annotationStore ? &annotationStore->annotations() : nullptr;
  if (!list || index < 0 || static_cast<size_t>(index) >= list->size()) return false;
  const Annotation& a = (*list)[static_cast<size_t>(index)];
  bool onPage = false;
  for (const auto& w : session->words) {
    if (w.visibleOffset >= a.startOffset && w.visibleOffset < a.endOffset) {
      onPage = true;
      break;
    }
  }
  if (!onPage) {
    LOG_INF("NTL", "Highlight %d not on the page shown; no note sheet", index);
    return false;
  }
  session->purpose = HintPurpose::Select;
  session->pageStored = storeHintPageLocked();
  hints = std::move(session);
  if (openNoteEditorLocked(index, false) && hints) {
    // No stored page to restore from: draw the sheet over the page as it is.
    hints->redrawAfterRender = false;
    drawHintOverlay();
    pushOverlayRefresh();
  }
  return true;
}

bool EpubReaderActivity::exportNotes(char* msg, const size_t msgSize) {
  if (!annotationStore || !epub) {
    snprintf(msg, msgSize, "%s", tr(STR_SEL_UNAVAILABLE));
    return false;
  }
  return deckpoint::reader::exportBookNotes(epub, *annotationStore, msg, msgSize);
}
