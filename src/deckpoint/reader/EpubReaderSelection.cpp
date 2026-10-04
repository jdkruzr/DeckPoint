// DECKPOINT: highlight selection in the reader, on the hint session
// (HintSession, purpose Select). Pure state lives in SelectionSession.h.
//
// Keyboard (`v`): hint labels pick the start word, then labels again pick the
// end word (Enter there: just the start word). The range is then shown
// inverted: Enter saves, `n` saves and opens the note sheet
// (EpubReaderNote.cpp), Backspace goes back a step, mic cancels. A start word
// inside an existing highlight opens that highlight's popup instead.
//
// Touch: a long-press opens Look up / Highlight / Note over the word (Edit
// note / Delete / Look up over a highlighted word; a plain tap on one does
// too). Highlight / Note make the word the anchor: tap the end word (the same
// word again: just that one), then Save on the bar (Note: then the sheet).
// Edit note opens the sheet on the highlight's note.
//
// A saved range that touches or overlaps existing highlights (ends inclusive,
// as the sync merge sees it) asks Extend highlight / Cancel first: Extend
// joins them into one entry (union range, notes joined in order, the old keys
// tombstoned). Only offered when every joined highlight lies on the page; a
// range inside one highlight just opens its note (`n`) or says so.
//
// Every stage draws over the clean page stored out of the framebuffer, like
// the `d` labels (EpubReaderHints.cpp): one FAST refresh per change, and
// cancelling on a gray page re-renders it so anti-aliasing comes back. A save
// or delete re-renders the page with its underlines. The bottom band shows the
// stage's key legend; `?` opens the selection help and the stage comes back
// over the re-rendered page when the help closes.

#include <ChapterXPathResolver.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

#include "CrossPointSettings.h"
#include "activities/RenderLock.h"
#include "activities/reader/EpubReaderActivity.h"
#include "components/UITheme.h"
#include "deckpoint/HelpKey.h"
#include "deckpoint/KeyHelp.h"
#include "deckpoint/KeyHelpActivity.h"
#include "deckpoint/KeyLegend.h"
#include "deckpoint/annotations/Annotation.h"
#include "deckpoint/annotations/AnnotationGeometry.h"
#include "deckpoint/annotations/AnnotationMerge.h"
#include "deckpoint/reader/WordHitTest.h"
#include "fontIds.h"

using deckpoint::annotations::AddResult;
using deckpoint::annotations::Annotation;
using deckpoint::annotations::AnnotationStore;
using deckpoint::annotations::Field;
using deckpoint::annotations::FIELD_COUNT;
using deckpoint::annotations::NoteMerge;
using deckpoint::annotations::Placement;
using deckpoint::reader::ActionMenu;
using deckpoint::reader::BarLayout;
using deckpoint::reader::ExtendPlan;
using deckpoint::reader::HintMatcher;
using deckpoint::reader::HintPurpose;
using deckpoint::reader::HintSession;
using deckpoint::reader::MenuLayout;
using deckpoint::reader::OffsetRange;
using deckpoint::reader::PlacedHighlight;
using deckpoint::reader::SelectionAction;
using deckpoint::reader::SelectionSession;
using deckpoint::reader::SelectionText;
using deckpoint::reader::SelectMode;
using deckpoint::reader::UiRect;
using deckpoint::reader::WordBox;
using deckpoint::reader::WordExtent;

namespace {

// Longest word copied out for a lookup (bytes, incl. NUL).
constexpr size_t MAX_LOOKUP_WORD = 64;
constexpr int MENU_FONT_ID = UI_12_FONT_ID;
constexpr int MENU_SMALL_FONT_ID = SMALL_FONT_ID;
constexpr int MENU_PAD = 4;
constexpr int MENU_BORDER = 2;
constexpr int MENU_SIDE_MARGIN = 8;
constexpr int MENU_MAX_WIDTH = 260;
// The highlight popup's header: note (or highlighted text) preview.
constexpr uint8_t HEADER_LINES = 2;
constexpr size_t HEADER_PREVIEW_BYTES = 160;

WordExtent extentOf(const WordBox& word) {
  return deckpoint::reader::wordExtent(word.visibleOffset, word.text, strlen(word.text));
}

const char* actionLabel(const SelectionAction action) {
  switch (action) {
    case SelectionAction::LookUp:
      return tr(STR_SEL_LOOK_UP);
    case SelectionAction::Highlight:
      return tr(STR_SEL_HIGHLIGHT);
    case SelectionAction::Note:
      return tr(STR_SEL_NOTE);
    case SelectionAction::EditNote:
      return tr(STR_SEL_EDIT_NOTE);
    case SelectionAction::Delete:
      return tr(STR_DELETE);
    case SelectionAction::Extend:
      return tr(STR_SEL_EXTEND);
    case SelectionAction::Cancel:
      return tr(STR_CANCEL);
    case SelectionAction::None:
    default:
      return "";
  }
}

// Up to maxBytes of `text` (whole UTF-8 sequences), line breaks as spaces.
void appendPreview(std::string& out, const std::string_view text, const size_t maxBytes) {
  size_t n = std::min(text.size(), maxBytes);
  while (n > 0 && n < text.size() && (static_cast<uint8_t>(text[n]) & 0xC0) == 0x80) n--;
  for (size_t i = 0; i < n; i++) out.push_back(text[i] == '\n' || text[i] == '\r' ? ' ' : text[i]);
  if (n < text.size()) out.append("\xE2\x80\xA6");  // …
}

bool isCancelKey(const freeink::KeyEvent& event) {
  return event.special == freeink::SpecialKey::Escape || event.special == freeink::SpecialKey::Backspace;
}

}  // namespace

// ---------------------------------------------------------------- opening

void EpubReaderActivity::openSelectionMenuLocked(const int word) {
  HintSession& s = *hints;
  s.menuWord = word;
  s.highlight = annotationStore ? annotationStore->highlightAt(currentSpineIndex, s.words[word].visibleOffset) : -1;
  const bool onHighlight = s.highlight >= 0;
  s.menu.open(onHighlight ? ActionMenu::Kind::Highlight : ActionMenu::Kind::Word);
  s.mode = onHighlight ? SelectMode::HighlightMenu : SelectMode::WordMenu;
}

bool EpubReaderActivity::openTouchSelectionLocked(std::unique_ptr<HintSession> session, const int word) {
  settleOverlayRefresh();
  if (hints) closeHintsLocked(false);
  session->purpose = HintPurpose::Select;
  session->touchOrigin = true;
  hints = std::move(session);
  hintsOpen = true;
  hintsSelect = true;
  hintOpenPending = false;
  readerKeys.reset();
  openSelectionMenuLocked(word);
  // A toast on the page would be stored under the popup: render a clean page
  // first, hintsAfterRender() puts the popup on it.
  const bool dirty = keyPopupShown || showBookmarkMessage || showDictionaryMessage;
  keyPopupShown = false;
  if (dirty) {
    showBookmarkMessage = false;
    showDictionaryMessage = false;
    hints->redrawAfterRender = true;
    return true;
  }
  hints->pageStored = storeHintPageLocked();
  drawHintOverlay();
  pushOverlayRefresh();
  return false;
}

// ---------------------------------------------------------------- drawing

void EpubReaderActivity::invertWordsLocked(const int from, const int to) {
  if (!hints || from < 0 || to < from || static_cast<size_t>(to) >= hints->words.size()) return;
  const std::vector<WordBox>& words = hints->words;
  const int fontId = SETTINGS.getReaderFontId();
  if (auto* fontCache = renderer.getFontCacheManager()) {
    // White glyphs come from the same cache; one prewarm for the whole run.
    std::string text;
    uint8_t styles = 0;
    for (int i = from; i <= to; i++) {
      text.append(words[i].text);
      text.push_back(' ');
      styles |= static_cast<uint8_t>(1u << (static_cast<uint8_t>(words[i].style) & 0x03));
    }
    fontCache->prewarmCache(fontId, text.c_str(), styles);
  }
  const int lineHeight = renderer.getLineHeight(fontId);
  int i = from;
  while (i <= to) {
    // One block per line, spanning the gaps between the line's words.
    int j = i;
    int left = words[i].x;
    int right = words[i].x + words[i].width;
    while (j < to && words[j + 1].row == words[i].row) {
      j++;
      left = std::min<int>(left, words[j].x);
      right = std::max<int>(right, words[j].x + words[j].width);
    }
    renderer.fillRect(left - 2, words[i].y - 1, right - left + 4, lineHeight + 2, true);
    for (int k = i; k <= j; k++)
      renderer.drawText(fontId, words[k].x, words[k].y, words[k].text, false, words[k].style);
    i = j + 1;
  }
}

MenuLayout EpubReaderActivity::selectionMenuLayout() const {
  const HintSession& s = *hints;
  const int screenW = renderer.getScreenWidth();
  const int width = std::min(screenW - 2 * MENU_SIDE_MARGIN, MENU_MAX_WIDTH);
  // Full-pitch rows, like the touch lists.
  const int rowH =
      std::max(UITheme::getInstance().getMetrics().listRowHeight, renderer.getLineHeight(MENU_FONT_ID) + 2 * MENU_PAD);
  const int headerH = s.mode == SelectMode::HighlightMenu
                          ? HEADER_LINES * renderer.getLineHeight(MENU_SMALL_FONT_ID) + 2 * MENU_PAD + MENU_BORDER
                          : MENU_BORDER;
  const WordBox& word = s.words[std::max(0, s.menuWord)];
  return deckpoint::reader::layoutMenu(screenW, renderer.getScreenHeight(), deckpoint::keyLegendBandHeight(renderer),
                                       word.y, renderer.getLineHeight(SETTINGS.getReaderFontId()), width, headerH, rowH,
                                       s.menu.count());
}

BarLayout EpubReaderActivity::selectionBarLayout() const {
  const HintSession& s = *hints;
  const int anchor = s.selection.anchor();
  const int anchorTop = anchor >= 0 ? s.words[anchor].y : 0;
  const int rowH =
      std::max(UITheme::getInstance().getMetrics().listRowHeight, renderer.getLineHeight(MENU_FONT_ID) + 2 * MENU_PAD);
  return deckpoint::reader::layoutBar(renderer.getScreenWidth(), renderer.getScreenHeight(),
                                      deckpoint::keyLegendBandHeight(renderer), anchorTop, rowH);
}

void EpubReaderActivity::drawSelectionMenu() const {
  const HintSession& s = *hints;
  const MenuLayout layout = selectionMenuLayout();
  const UiRect& box = layout.box;
  renderer.fillRect(box.x, box.y, box.w, box.h, false);
  renderer.drawRect(box.x, box.y, box.w, box.h, MENU_BORDER, true);
  const int innerX = box.x + MENU_BORDER + MENU_PAD;
  const int innerW = box.w - 2 * (MENU_BORDER + MENU_PAD);

  if (s.mode == SelectMode::HighlightMenu && annotationStore && s.highlight >= 0 &&
      static_cast<size_t>(s.highlight) < annotationStore->annotations().size()) {
    // The note, or the highlighted text in quotes when there is none.
    const Annotation& a = annotationStore->annotations()[static_cast<size_t>(s.highlight)];
    std::string preview;
    preview.reserve(HEADER_PREVIEW_BYTES + 8);
    if (a.has(Field::Note)) {
      appendPreview(preview, a.get(Field::Note), HEADER_PREVIEW_BYTES);
    } else {
      preview.append("\xE2\x80\x9C");  // “
      appendPreview(preview, a.get(Field::Text), HEADER_PREVIEW_BYTES);
      preview.append("\xE2\x80\x9D");  // ”
    }
    const int lineH = renderer.getLineHeight(MENU_SMALL_FONT_ID);
    const auto lines = renderer.wrappedText(MENU_SMALL_FONT_ID, preview.c_str(), innerW, HEADER_LINES);
    int y = box.y + MENU_BORDER + MENU_PAD;
    for (const auto& line : lines) {
      renderer.drawText(MENU_SMALL_FONT_ID, innerX, y, line.c_str(), true);
      y += lineH;
    }
    const int sepY = box.y + layout.headerH - 1;
    renderer.drawLine(box.x, sepY, box.x + box.w - 1, sepY, true);
  }

  const int menuLineH = renderer.getLineHeight(MENU_FONT_ID);
  const int smallLineH = renderer.getLineHeight(MENU_SMALL_FONT_ID);
  for (uint8_t i = 0; i < layout.rows; i++) {
    const UiRect row = layout.row(i);
    const bool cursor = i == s.menu.cursor();
    if (cursor) {
      renderer.fillRect(row.x + MENU_BORDER, row.y, row.w - 2 * MENU_BORDER,
                        std::min<int>(row.h, box.y + box.h - MENU_BORDER - row.y), true);
    }
    const SelectionAction action = s.menu.item(i);
    renderer.drawText(MENU_FONT_ID, innerX, row.y + (row.h - menuLineH) / 2, actionLabel(action), !cursor);
    const char key[2] = {ActionMenu::shortcut(action), '\0'};
    if (key[0] != '\0') {
      const int keyW = renderer.getTextWidth(MENU_SMALL_FONT_ID, key);
      renderer.drawText(MENU_SMALL_FONT_ID, innerX + innerW - keyW, row.y + (row.h - smallLineH) / 2, key, !cursor);
    }
  }
}

void EpubReaderActivity::drawSelectionLegend() const {
  const HintSession& s = *hints;
  const char* confirm = "";
  const char* extra = nullptr;
  const char* previous = "";
  const char* next = "";
  switch (s.mode) {
    case SelectMode::Labels:
      if (s.selection.stage() == SelectionSession::Stage::PickStart) {
        extra = tr(STR_SEL_LEGEND_START);
      } else {
        confirm = tr(STR_SEL_ONE_WORD);
        extra = tr(STR_SEL_LEGEND_END);
      }
      break;
    case SelectMode::TouchEnd:
      confirm = tr(STR_SEL_ONE_WORD);
      extra = tr(STR_SEL_LEGEND_TAP_END);
      break;
    case SelectMode::Range:
      confirm = tr(STR_SEL_SAVE);
      if (!s.withNote) extra = tr(STR_SEL_LEGEND_NOTE);
      break;
    case SelectMode::WordMenu:
    case SelectMode::HighlightMenu:
    case SelectMode::ExtendMenu:
      confirm = tr(STR_KH_CHOOSE);
      previous = tr(STR_DIR_UP);
      next = tr(STR_DIR_DOWN);
      break;
    case SelectMode::Note:
      // Dropped first at 240 px; the sheet's header shows it too.
      confirm = tr(STR_SEL_SAVE);
      extra = tr(STR_NOTE_HINT_NEWLINE);
      break;
  }
  // A bare '?' types in the note sheet: its help is Alt+v.
  const bool note = s.mode == SelectMode::Note;
  deckpoint::setLegendExtra(extra);
  if (note) deckpoint::setLegendHelp(tr(STR_NOTE_LEGEND_HELP));
  deckpoint::drawHintLegend(renderer, tr(STR_CANCEL), confirm, previous, next);
  if (note) deckpoint::setLegendHelp(nullptr);
  deckpoint::setLegendExtra(nullptr);
}

void EpubReaderActivity::drawSelectionOverlay() {
  if (!hints) return;
  HintSession& s = *hints;
  const auto drawBar = [&](const char* confirmLabel) {
    const BarLayout bar = selectionBarLayout();
    const int lineH = renderer.getLineHeight(MENU_FONT_ID);
    const auto button = [&](const UiRect& r, const char* label, const bool primary) {
      renderer.fillRect(r.x, r.y, r.w, r.h, primary);
      if (!primary) renderer.drawRect(r.x, r.y, r.w, r.h, MENU_BORDER, true);
      const int w = renderer.getTextWidth(MENU_FONT_ID, label);
      renderer.drawText(MENU_FONT_ID, r.x + (r.w - w) / 2, r.y + (r.h - lineH) / 2, label, !primary);
    };
    button(bar.cancel, tr(STR_CANCEL), false);
    button(bar.confirm, confirmLabel, true);
  };

  switch (s.mode) {
    case SelectMode::Labels:
      if (s.selection.anchor() >= 0) invertWordsLocked(s.selection.anchor(), s.selection.anchor());
      drawHintLabels();
      break;
    case SelectMode::TouchEnd:
      invertWordsLocked(s.selection.anchor(), s.selection.anchor());
      drawBar(tr(STR_SEL_ONE_WORD));
      break;
    case SelectMode::Range:
      invertWordsLocked(s.selection.first(), s.selection.last());
      if (s.touchOrigin) drawBar(tr(STR_SEL_SAVE));
      break;
    case SelectMode::WordMenu:
    case SelectMode::HighlightMenu:
      invertWordsLocked(s.menuWord, s.menuWord);
      drawSelectionMenu();
      break;
    case SelectMode::ExtendMenu:
      invertWordsLocked(s.selection.first(), s.selection.last());
      drawSelectionMenu();
      break;
    case SelectMode::Note:
      drawNoteSheet();
      s.noteDirty = false;
      break;
  }
  drawSelectionLegend();
}

// ---------------------------------------------------------------- ending

bool EpubReaderActivity::endSelectionLocked(const bool pageChanged, const char* toast) {
  bool rerender;
  if (pageChanged) {
    closeHintsLocked(false);
    rerender = true;
  } else {
    rerender = closeHintsLocked(true);
  }
  if (toast != nullptr) {
    if (rerender) {
      pendingHintToast = toast;
    } else {
      settleOverlayRefresh();
      showKeyPopupLocked(toast, true);
    }
  }
  return rerender;
}

bool EpubReaderActivity::saveSelectionLocked() {
  HintSession& s = *hints;
  const bool withNote = s.withNote;
  if (!epub || !annotationStore || annotationStore->readOnly())
    return endSelectionLocked(false, tr(STR_SEL_UNAVAILABLE));
  const int a = s.selection.first();
  const int b = s.selection.last();
  if (a < 0 || static_cast<size_t>(b) >= s.words.size()) return endSelectionLocked(false, nullptr);
  OffsetRange range = SelectionSession::span(extentOf(s.words[a]), extentOf(s.words[b]));
  if (range.empty()) return endSelectionLocked(false, tr(STR_SEL_SAVE_FAILED));

  // Existing highlights the range touches (KOReader's rule: the sync would
  // collapse them): offer to extend instead of stacking a second one.
  ExtendPlan plan;
  {
    const auto& list = annotationStore->annotations();
    std::vector<PlacedHighlight> placed;
    size_t inChapter = 0;
    for (size_t i = 0; i < list.size(); i++) inChapter += list[i].spineIndex == currentSpineIndex ? 1 : 0;
    placed.reserve(inChapter);
    for (size_t i = 0; i < list.size(); i++) {
      const Annotation& h = list[i];
      if (h.spineIndex != currentSpineIndex || h.deleted || h.placement != Placement::Resolved) continue;
      placed.push_back({h.startOffset, h.endOffset, static_cast<int>(i)});
    }
    OffsetRange page{UINT32_MAX, 0};
    for (const WordBox& w : s.words) {
      const WordExtent e = extentOf(w);
      page.start = std::min(page.start, e.start);
      page.end = std::max(page.end, e.end);
    }
    plan = deckpoint::reader::planExtend(range, placed.data(), placed.size(), page);
  }
  switch (plan.kind) {
    case ExtendPlan::Kind::Covered:
      if (withNote) return openNoteEditorLocked(plan.indices[0], false);
      return endSelectionLocked(false, tr(STR_SEL_ALREADY));
    case ExtendPlan::Kind::Extend:
      if (!s.extendChosen) {
        s.mode = SelectMode::ExtendMenu;
        s.menuWord = a;
        s.menu.open(ActionMenu::Kind::Extend);
        return repaintHintsLocked();
      }
      range = plan.range;
      break;
    case ExtendPlan::Kind::OffPage:
    case ExtendPlan::Kind::TooMany:
      // Saved alongside; the next sync collapses the overlap, keeping notes.
      LOG_INF("ANN", "Touched highlights not joinable on this page (%s)",
              plan.kind == ExtendPlan::Kind::OffPage ? "off page" : "too many");
      plan = ExtendPlan{};
      break;
    case ExtendPlan::Kind::None:
    default:
      break;
  }
  const bool extending = plan.kind == ExtendPlan::Kind::Extend;

  // Notes of the joined highlights, in position order, as the sync merge
  // joins them. Refused rather than cut: this is the user's own text.
  std::string note;
  std::string earliest;  // creation stamp of the oldest joined highlight
  const char* drawer = deckpoint::annotations::OWN_DRAWER;
  const char* color = deckpoint::annotations::OWN_COLOR;
  std::string keptDrawer;
  std::string keptColor;
  if (extending) {
    const auto& list = annotationStore->annotations();
    for (uint8_t k = 0; k < plan.count; k++) {
      const Annotation& h = list[static_cast<size_t>(plan.indices[k])];
      const NoteMerge m = deckpoint::annotations::mergeNoteText(note, h.get(Field::Note));
      if (m == NoteMerge::Truncated || m == NoteMerge::NoRoom) {
        return endSelectionLocked(false, tr(STR_SEL_EXTEND_TOO_LONG));
      }
      const std::string_view created = h.get(Field::Datetime);
      if (!created.empty() && created != deckpoint::annotations::UNSET_TIMESTAMP &&
          (earliest.empty() || created < earliest)) {
        earliest.assign(created.data(), created.size());
      }
      // The first joined highlight's style (a KOReader one keeps its look).
      if (k == 0 && h.has(Field::Drawer)) keptDrawer.assign(h.get(Field::Drawer));
      if (k == 0 && h.has(Field::Color)) keptColor.assign(h.get(Field::Color));
    }
    if (!keptDrawer.empty()) drawer = keptDrawer.c_str();
    if (!keptColor.empty()) color = keptColor.c_str();
  }

  // Highlight text from every token of the page in range, punctuation tokens
  // included (transient, at most MAX_TEXT_BYTES).
  std::string text;
  text.reserve(std::min<size_t>(range.end - range.start + 16, deckpoint::annotations::MAX_TEXT_BYTES));
  {
    SelectionText builder(range, deckpoint::annotations::MAX_TEXT_BYTES, text);
    for (const auto& element : s.page->elements) {
      if (element->getTag() != TAG_PageLine) continue;
      const TextBlock* block = static_cast<const PageLine*>(element.get())->getBlock();
      if (!block || !block->valid()) continue;
      for (uint16_t i = 0; i < block->wordCount(); i++) {
        if (block->wordHasVisibleOffset(i)) {
          builder.add(block->wordVisibleOffset(i), block->wordText(i), block->wordTextLen(i));
        }
      }
    }
    if (builder.truncated()) LOG_INF("ANN", "Highlight text cut at %u bytes", static_cast<unsigned>(text.size()));
  }

  using KOReaderXPointer::Bias;
  using KOReaderXPointer::Style;
  // Legacy spelling (KOReader with crengine DOM < 20260812); pos1 is exclusive.
  const std::string pos0 = ChapterXPathResolver::findXPathForVisibleTextOffset(epub, currentSpineIndex, range.start,
                                                                               Bias::Start, Style::Legacy);
  const std::string pos1 =
      ChapterXPathResolver::findXPathForVisibleTextOffset(epub, currentSpineIndex, range.end, Bias::End, Style::Legacy);
  if (pos0.empty() || pos1.empty()) {
    LOG_ERR("ANN", "No XPointer for offsets %u..%u", static_cast<unsigned>(range.start),
            static_cast<unsigned>(range.end));
    return endSelectionLocked(false, tr(STR_SEL_SAVE_FAILED));
  }

  const std::string chapter = currentChapterTitle();
  char now[20];
  AnnotationStore::now(now);
  std::string_view values[FIELD_COUNT];
  values[static_cast<size_t>(Field::Pos0)] = pos0;
  values[static_cast<size_t>(Field::Pos1)] = pos1;
  values[static_cast<size_t>(Field::Page)] = pos0;
  values[static_cast<size_t>(Field::Text)] = text;
  values[static_cast<size_t>(Field::Chapter)] = chapter;
  values[static_cast<size_t>(Field::Drawer)] = drawer;
  values[static_cast<size_t>(Field::Color)] = color;
  values[static_cast<size_t>(Field::Datetime)] = now;
  if (extending) {
    values[static_cast<size_t>(Field::Note)] = note;
    if (!earliest.empty()) values[static_cast<size_t>(Field::Datetime)] = earliest;
    values[static_cast<size_t>(Field::DatetimeUpdated)] = now;
  }
  Annotation annotation;
  const bool assigned = annotation.assign(values);
  const std::string key = assigned ? deckpoint::annotations::annotationKey(annotation) : std::string();
  if (!assigned) {
    LOG_ERR("ANN", "OOM: new highlight");
    return endSelectionLocked(false, tr(STR_SEL_SAVE_FAILED));
  }
  const AddResult result =
      extending ? annotationStore->replacePlacedAndSave(std::move(annotation), currentSpineIndex, range.start,
                                                        range.end, plan.indices, plan.count)
                : annotationStore->addPlacedAndSave(std::move(annotation), currentSpineIndex, range.start, range.end);
  const bool stored = result == AddResult::Added || result == AddResult::Replaced;
  LOG_INF("ANN", "Highlight %s%s: %s||%s \"%s\"", stored ? "saved" : "rejected", extending ? " (extended)" : "",
          pos0.c_str(), pos1.c_str(), text.c_str());
  if (!stored) return endSelectionLocked(false, tr(STR_SEL_SAVE_FAILED));
  if (withNote) return openNoteEditorLocked(annotationStore->annotations().find(key), true);
  return endSelectionLocked(true, nullptr);
}

void EpubReaderActivity::runSelectionActionLocked(const SelectionAction action, bool* rerender, char* lookUp,
                                                  const size_t lookUpSize) {
  HintSession& s = *hints;
  switch (action) {
    case SelectionAction::LookUp: {
      if (SETTINGS.dictionaryName[0] == '\0') {
        *rerender = endSelectionLocked(false, tr(STR_DICT_NO_DICT_SET));
        return;
      }
      // As the `d` labels: the word inverted on the clean page, then the
      // lookup's popup pushes both.
      const WordBox box = s.words[s.menuWord];
      snprintf(lookUp, lookUpSize, "%s", box.text);
      settleOverlayRefresh();
      if (s.pageStored) renderer.restoreBwBuffer(/*resyncPanelBaseline=*/false);
      s.pageStored = false;
      markPickedWordLocked(box);
      hints.reset();
      hintsOpen = false;
      hintsSelect = false;
      return;
    }
    case SelectionAction::Highlight:
    case SelectionAction::Note:  // pick the range first; the sheet follows the save
      if (!annotationStore || annotationStore->readOnly()) {
        *rerender = endSelectionLocked(false, tr(STR_SEL_UNAVAILABLE));
        return;
      }
      s.withNote = action == SelectionAction::Note;
      s.selection.beginAt(s.menuWord);
      if (s.touchOrigin) {
        s.mode = SelectMode::TouchEnd;
      } else {
        s.mode = SelectMode::Labels;
        s.matcher.begin(
            static_cast<uint16_t>(std::min<size_t>(s.words.size(), deckpoint::reader::HintLabels::MAX_TARGETS)));
      }
      *rerender = repaintHintsLocked();
      return;
    case SelectionAction::EditNote:
      if (!annotationStore || annotationStore->readOnly()) {
        *rerender = endSelectionLocked(false, tr(STR_SEL_UNAVAILABLE));
        return;
      }
      *rerender = openNoteEditorLocked(s.highlight, false);
      return;
    case SelectionAction::Delete: {
      const bool deleted =
          annotationStore && s.highlight >= 0 && annotationStore->deleteAndSave(static_cast<size_t>(s.highlight));
      *rerender = deleted ? endSelectionLocked(true, nullptr) : endSelectionLocked(false, tr(STR_SEL_DELETE_FAILED));
      return;
    }
    case SelectionAction::Extend:
      s.extendChosen = true;
      *rerender = saveSelectionLocked();
      return;
    case SelectionAction::Cancel:
      *rerender = closeHintsLocked(true);
      return;
    case SelectionAction::None:
    default:
      return;
  }
}

// ---------------------------------------------------------------- input

void EpubReaderActivity::selectionKey(const freeink::KeyEvent& event) {
  char word[MAX_LOOKUP_WORD];
  word[0] = '\0';
  bool rerender = false;
  bool passThrough = false;
  bool help = false;
  bool noteHelp = false;
  {
    RenderLock lock;
    if (!hints || hints->stale) {
      // Same as the `d` labels: wait for a pending open, else the key goes
      // back to the reader.
      if (hintOpenPending && !isCancelKey(event)) return;
      passThrough = !hintOpenPending;
      closeHintsLocked(false);
    } else if (hints->mode == SelectMode::Note) {
      // The sheet takes every key ('?' types); Alt+v is its help.
      rerender = noteKeyLocked(event, &noteHelp);
      if (noteHelp && hints) {
        settleOverlayRefresh();
        if (hints->pageStored) {
          renderer.discardStoredBwBuffer();
          hints->pageStored = false;
        }
        hints->redrawAfterRender = true;
      }
    } else if (deckpoint::isHelpKey(event)) {
      // The help screen covers the page; the stage is drawn again over the
      // page render that follows it.
      settleOverlayRefresh();
      if (hints->pageStored) {
        renderer.discardStoredBwBuffer();
        hints->pageStored = false;
      }
      hints->redrawAfterRender = true;
      help = true;
    } else {
      HintSession& s = *hints;
      const uint16_t targets =
          static_cast<uint16_t>(std::min<size_t>(s.words.size(), deckpoint::reader::HintLabels::MAX_TARGETS));
      switch (s.mode) {
        case SelectMode::Labels: {
          if (event.special == freeink::SpecialKey::Enter) {
            if (s.selection.pickSingle()) {
              s.mode = SelectMode::Range;
              rerender = repaintHintsLocked();
            }
            break;
          }
          const bool typedSome = s.matcher.typed()[0] != '\0';
          switch (s.matcher.feed(event)) {
            case HintMatcher::Result::Narrowed:
              rerender = repaintHintsLocked();
              break;
            case HintMatcher::Result::Cancelled:
              if (event.special == freeink::SpecialKey::Escape) {
                rerender = closeHintsLocked(true);
              } else if (event.special == freeink::SpecialKey::Backspace) {
                // Nothing typed: back from the end word to the start word.
                if (s.selection.back()) {
                  s.matcher.begin(targets);
                  rerender = repaintHintsLocked();
                } else {
                  rerender = closeHintsLocked(true);
                }
              } else if (typedSome) {
                // A letter no label continues with: start the label over.
                s.matcher.begin(targets);
                rerender = repaintHintsLocked();
              }
              break;
            case HintMatcher::Result::Selected: {
              const int picked = s.matcher.selected();
              if (s.selection.stage() == SelectionSession::Stage::PickStart && annotationStore &&
                  annotationStore->highlightAt(currentSpineIndex, s.words[picked].visibleOffset) >= 0) {
                openSelectionMenuLocked(picked);
              } else if (s.selection.stage() == SelectionSession::Stage::PickStart) {
                s.selection.pick(picked);
                s.matcher.begin(targets);
              } else {
                s.selection.pick(picked);
                s.mode = SelectMode::Range;
              }
              rerender = repaintHintsLocked();
              break;
            }
            case HintMatcher::Result::Ignored:
            default:
              break;
          }
          break;
        }
        case SelectMode::TouchEnd:
          if (event.special == freeink::SpecialKey::Enter) {
            s.selection.pickSingle();
            s.mode = SelectMode::Range;
            rerender = repaintHintsLocked();
          } else if (isCancelKey(event)) {
            rerender = closeHintsLocked(true);
          }
          break;
        case SelectMode::Range:
          if (event.special == freeink::SpecialKey::Enter) {
            rerender = saveSelectionLocked();
          } else if (event.special == freeink::SpecialKey::None && (event.ch == 'n' || event.ch == 'N')) {
            s.withNote = true;
            rerender = saveSelectionLocked();
          } else if (event.special == freeink::SpecialKey::Escape) {
            rerender = closeHintsLocked(true);
          } else if (event.special == freeink::SpecialKey::Backspace) {
            // Re-pick the end word.
            s.selection.back();
            if (s.touchOrigin) {
              s.mode = SelectMode::TouchEnd;
            } else {
              s.mode = SelectMode::Labels;
              s.matcher.begin(targets);
            }
            rerender = repaintHintsLocked();
          }
          break;
        case SelectMode::WordMenu:
        case SelectMode::HighlightMenu:
        case SelectMode::ExtendMenu:
          switch (s.menu.feed(event)) {
            case ActionMenu::Result::Moved:
              rerender = repaintHintsLocked();
              break;
            case ActionMenu::Result::Cancelled:
              rerender = closeHintsLocked(true);
              break;
            case ActionMenu::Result::Chosen:
              runSelectionActionLocked(s.menu.chosen(), &rerender, word, sizeof(word));
              break;
            case ActionMenu::Result::Ignored:
            default:
              break;
          }
          break;
        case SelectMode::Note:  // handled above
          break;
      }
    }
  }
  if (rerender) requestUpdate();
  if (help) {
    keysSuspended = true;
    deckpoint::openKeyHelp(renderer, mappedInput, deckpoint::SELECTION_HELP_OWNER, &deckpoint::SELECTION_KEY_HELP);
    return;
  }
  if (noteHelp) {
    keysSuspended = true;
    deckpoint::openKeyHelp(renderer, mappedInput, deckpoint::NOTE_EDITOR_HELP_OWNER, &deckpoint::NOTE_EDITOR_KEY_HELP);
    return;
  }
  if (passThrough) {
    runReaderCommand(readerKeys.feed(event));
    return;
  }
  if (word[0] != '\0') lookUpPickedWord(word);
}

void EpubReaderActivity::selectionTouch(const int x, const int y) {
  char word[MAX_LOOKUP_WORD];
  word[0] = '\0';
  bool rerender = false;
  {
    RenderLock lock;
    if (!hints || hints->stale) {
      closeHintsLocked(false);
      return;
    }
    HintSession& s = *hints;
    const bool swipe = x < 0;
    const auto wordAt = [&]() {
      return deckpoint::reader::findWordAt(s.words.data(), s.words.size(), x, y,
                                           renderer.getLineHeight(SETTINGS.getReaderFontId()),
                                           deckpoint::reader::WORD_TOUCH_SLOP_PX);
    };
    switch (s.mode) {
      case SelectMode::Labels:
        // Keyboard label stages: a touch cancels, like every keyboard mode.
        rerender = closeHintsLocked(true);
        break;
      case SelectMode::TouchEnd:
      case SelectMode::Range: {
        if (swipe || !s.touchOrigin) {
          rerender = closeHintsLocked(true);
          break;
        }
        const int button = selectionBarLayout().hit(x, y);
        if (button == BarLayout::CANCEL) {
          rerender = closeHintsLocked(true);
        } else if (button == BarLayout::CONFIRM) {
          if (s.mode == SelectMode::TouchEnd) s.selection.pickSingle();
          rerender = saveSelectionLocked();
        } else {
          const int hit = wordAt();
          if (hit < 0) break;  // blank space: keep picking
          if (s.mode == SelectMode::TouchEnd && hit == s.selection.anchor()) {
            s.selection.pickSingle();
          } else {
            s.selection.pick(hit);
          }
          s.mode = SelectMode::Range;
          rerender = repaintHintsLocked();
        }
        break;
      }
      case SelectMode::Note:
        rerender = noteTouchLocked(x, y);
        break;
      case SelectMode::WordMenu:
      case SelectMode::HighlightMenu:
      case SelectMode::ExtendMenu: {
        if (swipe) {
          rerender = closeHintsLocked(true);
          break;
        }
        const int row = selectionMenuLayout().hit(x, y);
        if (row == MenuLayout::OUTSIDE) {
          rerender = closeHintsLocked(true);
        } else if (row >= 0 && s.menu.choose(row)) {
          runSelectionActionLocked(s.menu.chosen(), &rerender, word, sizeof(word));
        }
        break;
      }
    }
  }
  if (rerender) requestUpdate();
  if (word[0] != '\0') lookUpPickedWord(word);
}
