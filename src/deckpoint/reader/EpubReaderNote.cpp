// DECKPOINT: the note sheet. A panel over ~40% of the screen, below the
// highlighted passage (above it when the passage sits low), so the page and
// its underline stay in view while the note is typed. Editing model:
// deckpoint/NoteEditor (host-tested); this file draws it and feeds it keys.
//
// Keys: typing inserts, Enter saves, Shift+Enter (or Alt+Enter) is a new line,
// Backspace / Shift+Backspace delete, Alt+Backspace clears, Alt+h / Alt+l /
// Alt+k / Alt+j move the cursor, Alt+v is help (a bare '?' types). The mic key
// cancels; with unsaved changes the first press only asks ("Discard? Cancel
// again"). Touch: Cancel / Save buttons in the sheet's header row, a tap in
// the text moves the cursor, anything else is ignored.
//
// Refresh: the clean page stays stored (as for the other selection stages);
// a key only edits the model and marks the sheet dirty. noteSheetTick()
// redraws just the sheet into the framebuffer and pushes one FAST refresh as
// soon as the panel is idle, so keys typed during a refresh land together in
// the next one. Closing restores the stored page (gray pages re-render).

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>
#include <string_view>

#include "CrossPointSettings.h"
#include "activities/RenderLock.h"
#include "activities/reader/EpubReaderActivity.h"
#include "components/UITheme.h"
#include "deckpoint/KeyLegend.h"
#include "deckpoint/annotations/Annotation.h"
#include "fontIds.h"

using deckpoint::NoteEditor;
using deckpoint::TextMeasure;
using deckpoint::annotations::AddResult;
using deckpoint::annotations::Annotation;
using deckpoint::annotations::Field;
using deckpoint::reader::HintSession;
using deckpoint::reader::SelectMode;
using deckpoint::reader::UiRect;
using deckpoint::reader::WordBox;

namespace {

constexpr int NOTE_FONT_ID = UI_12_FONT_ID;
constexpr int BUTTON_FONT_ID = UI_12_FONT_ID;
constexpr int HINT_FONT_ID = SMALL_FONT_ID;
constexpr int BORDER = 2;
constexpr int PAD = 4;
constexpr int BUTTON_PAD_X = 6;
constexpr int SCROLLBAR_W = 3;
constexpr int CURSOR_W = 2;
constexpr int MIN_TEXT_LINES = 3;
// Sheet height as a fraction of the screen.
constexpr int SHEET_NUM = 2;
constexpr int SHEET_DEN = 5;
// Bytes measured per getTextWidth call (the renderer wants a C string).
constexpr size_t MEASURE_CHUNK = 64;

struct MeasureCtx {
  const GfxRenderer* renderer;
  int fontId;
};

// Sum of chunk widths, split on code point boundaries (no heap).
int measureText(void* ctx, const char* text, const size_t bytes) {
  const auto* m = static_cast<const MeasureCtx*>(ctx);
  char chunk[MEASURE_CHUNK + 1];
  int width = 0;
  size_t pos = 0;
  while (pos < bytes) {
    size_t n = std::min(bytes - pos, MEASURE_CHUNK);
    if (pos + n < bytes) {
      while (n > 1 && (static_cast<uint8_t>(text[pos + n]) & 0xC0) == 0x80) n--;
    }
    memcpy(chunk, text + pos, n);
    chunk[n] = '\0';
    width += m->renderer->getTextWidth(m->fontId, chunk);
    pos += n;
  }
  return width;
}

bool isSpace(const char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }

// The note as saved: trailing blanks dropped, and only blanks counts as none.
std::string_view trimmedNote(std::string_view text) {
  while (!text.empty() && isSpace(text.back())) text.remove_suffix(1);
  size_t lead = 0;
  while (lead < text.size() && isSpace(text[lead])) lead++;
  return lead == text.size() ? std::string_view() : text;
}

}  // namespace

struct EpubReaderActivity::NoteSheetGeometry {
  UiRect sheet;
  UiRect cancel;
  UiRect save;
  UiRect text;  // the wrapped lines
  int lineH;
  int visibleLines;
};

EpubReaderActivity::NoteSheetGeometry EpubReaderActivity::noteSheetGeometry() const {
  const HintSession& s = *hints;
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int bottomReserve = deckpoint::keyLegendBandHeight(renderer);
  const int lineH = renderer.getLineHeight(NOTE_FONT_ID);
  const int rowH =
      std::max(UITheme::getInstance().getMetrics().listRowHeight, renderer.getLineHeight(BUTTON_FONT_ID) + 2 * PAD);
  const int minH = rowH + MIN_TEXT_LINES * lineH + 2 * PAD + BORDER;
  const int sheetH = std::min(std::max(screenH * SHEET_NUM / SHEET_DEN, minH), screenH - bottomReserve);
  const deckpoint::SheetPlacement place =
      deckpoint::placeNoteSheet(screenH, bottomReserve, s.passageTop, s.passageBottom, sheetH);

  NoteSheetGeometry g{};
  g.sheet = {0, place.y, static_cast<int16_t>(screenW), place.h};
  const int buttonW = std::max(renderer.getTextWidth(BUTTON_FONT_ID, tr(STR_CANCEL)),
                               renderer.getTextWidth(BUTTON_FONT_ID, tr(STR_SEL_SAVE))) +
                      2 * BUTTON_PAD_X;
  g.cancel = {0, place.y, static_cast<int16_t>(buttonW), static_cast<int16_t>(rowH)};
  g.save = {static_cast<int16_t>(screenW - buttonW), place.y, static_cast<int16_t>(buttonW),
            static_cast<int16_t>(rowH)};
  const int textX = BORDER + PAD;
  const int textY = place.y + rowH + PAD;
  const int textW = screenW - 2 * (BORDER + PAD) - SCROLLBAR_W - CURSOR_W;
  const int textH = place.y + place.h - BORDER - PAD - textY;
  g.text = {static_cast<int16_t>(textX), static_cast<int16_t>(textY), static_cast<int16_t>(textW),
            static_cast<int16_t>(std::max(0, textH))};
  g.lineH = lineH;
  g.visibleLines = std::max(1, textH / std::max(1, lineH));
  return g;
}

// ---------------------------------------------------------------- open / close

bool EpubReaderActivity::openNoteEditorLocked(const int highlight, const bool pageChanged) {
  HintSession& s = *hints;
  if (!annotationStore || highlight < 0 || static_cast<size_t>(highlight) >= annotationStore->annotations().size()) {
    return endSelectionLocked(pageChanged, tr(STR_NOTE_SAVE_FAILED));
  }
  const Annotation& a = annotationStore->annotations()[static_cast<size_t>(highlight)];
  // The buffer (cap + 1 bytes, 2 KB) lives until the sheet closes.
  if (!s.note.open(a.get(Field::Note), deckpoint::annotations::MAX_NOTE_BYTES)) {
    LOG_ERR("ANN", "OOM: note editor (%u bytes)", static_cast<unsigned>(deckpoint::annotations::MAX_NOTE_BYTES));
    return endSelectionLocked(pageChanged, tr(STR_NOTE_LOW_MEMORY));
  }

  // The passage's lines on this page, for the sheet's placement.
  const int lineH = renderer.getLineHeight(SETTINGS.getReaderFontId());
  int top = INT16_MAX;
  int bottom = 0;
  for (const WordBox& w : s.words) {
    if (w.visibleOffset < a.startOffset || w.visibleOffset >= a.endOffset) continue;
    top = std::min<int>(top, w.y);
    bottom = std::max<int>(bottom, w.y + lineH);
  }
  if (top > bottom) top = bottom = 0;  // not on this page (cannot happen from a pick)
  s.passageTop = static_cast<int16_t>(top);
  s.passageBottom = static_cast<int16_t>(bottom);

  s.mode = SelectMode::Note;
  s.highlight = highlight;
  s.discardArmed = false;
  s.sheetMessage = nullptr;
  s.noteDirty = false;
  relayoutNoteLocked();
  LOG_DBG("ANN", "Note editor on %d (%u bytes)", highlight, static_cast<unsigned>(s.note.size()));
  if (pageChanged) {
    // The new underline needs a page render; the sheet goes on top of it.
    s.redrawAfterRender = true;
    return true;
  }
  return repaintHintsLocked();
}

void EpubReaderActivity::relayoutNoteLocked() {
  HintSession& s = *hints;
  const NoteSheetGeometry g = noteSheetGeometry();
  MeasureCtx ctx{&renderer, NOTE_FONT_ID};
  s.note.layout(TextMeasure{&ctx, &measureText}, g.text.w);
  s.note.scrollToCursor(static_cast<size_t>(g.visibleLines));
}

bool EpubReaderActivity::saveNoteLocked() {
  HintSession& s = *hints;
  if (!s.note.edited()) return closeHintsLocked(true);  // nothing changed
  const std::string_view note = trimmedNote(s.note.text());
  const AddResult result = annotationStore && s.highlight >= 0
                               ? annotationStore->setNoteAndSave(static_cast<size_t>(s.highlight), note)
                               : AddResult::NotPlaceable;
  if (result != AddResult::Replaced) {
    // Keep the sheet (and the text) up with the reason.
    s.sheetMessage = result == AddResult::TooLong ? tr(STR_NOTE_FULL) : tr(STR_NOTE_SAVE_FAILED);
    s.noteDirty = true;
    return false;
  }
  LOG_INF("ANN", "Note %s on %d (%u bytes)", note.empty() ? "removed" : "saved", s.highlight,
          static_cast<unsigned>(note.size()));
  // Re-render: the note marker appears (or goes).
  return endSelectionLocked(true, nullptr);
}

bool EpubReaderActivity::cancelNoteLocked() {
  HintSession& s = *hints;
  if (s.note.edited() && !s.discardArmed) {
    s.discardArmed = true;
    s.sheetMessage = tr(STR_NOTE_DISCARD_AGAIN);
    s.noteDirty = true;
    return false;
  }
  return closeHintsLocked(true);
}

// ---------------------------------------------------------------- drawing

void EpubReaderActivity::drawNoteSheet() const {
  const HintSession& s = *hints;
  const NoteSheetGeometry g = noteSheetGeometry();
  const UiRect& r = g.sheet;
  renderer.fillRect(r.x, r.y, r.w, r.h, false);
  renderer.drawRect(r.x, r.y, r.w, r.h, BORDER, true);

  // Header row: [Cancel]  hint or message  [Save]
  const int buttonLineH = renderer.getLineHeight(BUTTON_FONT_ID);
  const auto button = [&](const UiRect& b, const char* label, const bool primary) {
    renderer.fillRect(b.x, b.y, b.w, b.h, primary);
    if (!primary) renderer.drawRect(b.x, b.y, b.w, b.h, BORDER, true);
    const int w = renderer.getTextWidth(BUTTON_FONT_ID, label);
    renderer.drawText(BUTTON_FONT_ID, b.x + (b.w - w) / 2, b.y + (b.h - buttonLineH) / 2, label, !primary);
  };
  button(g.cancel, tr(STR_CANCEL), false);
  button(g.save, tr(STR_SEL_SAVE), true);
  const int midX = g.cancel.x + g.cancel.w + PAD;
  const int midW = g.save.x - PAD - midX;
  const char* mid = s.sheetMessage ? s.sheetMessage : tr(STR_NOTE_HINT_NEWLINE);
  if (midW > 0) {
    const int hintLineH = renderer.getLineHeight(HINT_FONT_ID);
    const auto lines = renderer.wrappedText(HINT_FONT_ID, mid, midW, 2);
    int y = g.cancel.y + (g.cancel.h - static_cast<int>(lines.size()) * hintLineH) / 2;
    for (const auto& line : lines) {
      const int w = renderer.getTextWidth(HINT_FONT_ID, line.c_str());
      renderer.drawText(HINT_FONT_ID, midX + (midW - w) / 2, y, line.c_str(), true);
      y += hintLineH;
    }
  }
  const int sepY = g.cancel.y + g.cancel.h;
  renderer.drawLine(r.x, sepY, r.x + r.w - 1, sepY, true);

  // Text: visible lines, the cursor as a bar.
  const NoteEditor& ed = s.note;
  MeasureCtx ctx{&renderer, NOTE_FONT_ID};
  const TextMeasure measure{&ctx, &measureText};
  const std::string_view text = ed.text();
  const size_t first = ed.scrollTop();
  const size_t last = std::min(ed.lineCount(), first + static_cast<size_t>(g.visibleLines));
  const size_t cursorLine = ed.cursorLine();
  char lineBuf[MEASURE_CHUNK + 1];
  for (size_t i = first; i < last; i++) {
    const NoteEditor::Line& line = ed.line(i);
    const int y = g.text.y + static_cast<int>(i - first) * g.lineH;
    // Drawn in chunks (no heap); a chunk's width places the next one.
    int x = g.text.x;
    size_t pos = line.start;
    while (pos < line.end) {
      size_t n = std::min<size_t>(line.end - pos, MEASURE_CHUNK);
      if (pos + n < line.end) {
        while (n > 1 && (static_cast<uint8_t>(text[pos + n]) & 0xC0) == 0x80) n--;
      }
      memcpy(lineBuf, text.data() + pos, n);
      lineBuf[n] = '\0';
      renderer.drawText(NOTE_FONT_ID, x, y, lineBuf, true);
      x += renderer.getTextWidth(NOTE_FONT_ID, lineBuf);
      pos += n;
    }
    if (i == cursorLine) {
      const int cx = std::min<int>(g.text.x + ed.cursorX(measure), g.text.x + g.text.w);
      renderer.fillRect(cx, y + 1, CURSOR_W, g.lineH - 2, true);
    }
  }

  // Scroll thumb when the note is longer than the sheet.
  if (ed.lineCount() > static_cast<size_t>(g.visibleLines)) {
    const int trackX = r.x + r.w - BORDER - SCROLLBAR_W - 1;
    const int trackH = g.text.h;
    const int total = static_cast<int>(ed.lineCount());
    const int thumbH = std::max(4, trackH * g.visibleLines / total);
    const int thumbY = g.text.y + (trackH - thumbH) * static_cast<int>(first) / std::max(1, total - g.visibleLines);
    renderer.fillRect(trackX, thumbY, SCROLLBAR_W, thumbH, true);
  }
}

void EpubReaderActivity::noteSheetTick() {
  RenderLock lock(RenderLock::Mode::Try);
  if (!lock.ownsLock() || !hints || hints->mode != SelectMode::Note || !hints->noteDirty) return;
  if (hints->stale || hints->redrawAfterRender) return;
  // The async refresh owns the framebuffer until the panel is done; keys
  // typed meanwhile only touched the model and go out together next time.
  if (overlayRefreshPending && renderer.refreshBusy()) return;
  hints->noteDirty = false;
  settleOverlayRefresh();
  drawNoteSheet();
  pushOverlayRefresh();
}

// ---------------------------------------------------------------- input

bool EpubReaderActivity::noteKeyLocked(const freeink::KeyEvent& event, bool* help) {
  using freeink::SpecialKey;
  HintSession& s = *hints;
  NoteEditor& ed = s.note;
  const bool alt = (event.mods & freeink::KeyMod::Alt) != 0;
  const bool shift = (event.mods & freeink::KeyMod::Shift) != 0;
  MeasureCtx ctx{&renderer, NOTE_FONT_ID};
  const TextMeasure measure{&ctx, &measureText};

  if (event.special == SpecialKey::Escape) return cancelNoteLocked();
  if (event.special == SpecialKey::Enter && !shift && !alt) return saveNoteLocked();
  // Any other key takes a pending message (and the discard question) down.
  if (s.sheetMessage) {
    s.sheetMessage = nullptr;
    s.noteDirty = true;
  }
  s.discardArmed = false;

  NoteEditor::Edit edit = NoteEditor::Edit::Unchanged;
  bool moved = false;
  switch (event.special) {
    case SpecialKey::Enter:  // Shift / Alt
      edit = ed.newline();
      break;
    case SpecialKey::Backspace:
      edit = alt ? ed.clear() : ed.backspace();
      break;
    case SpecialKey::Delete:  // Shift+Backspace
      edit = ed.deleteForward();
      break;
    case SpecialKey::Left:
      moved = ed.left();
      break;
    case SpecialKey::Right:
      moved = ed.right();
      break;
    case SpecialKey::Up:
      moved = ed.up(measure);
      break;
    case SpecialKey::Down:
      moved = ed.down(measure);
      break;
    case SpecialKey::Home:
      moved = ed.home();
      break;
    case SpecialKey::End:
      moved = ed.end();
      break;
    case SpecialKey::None:
      if (event.ch == 0) break;
      if (!alt) {
        edit = ed.insertChar(event.ch);
        break;
      }
      switch (event.ch) {
        case 'v':
        case 'V':
        case '?':
          *help = true;
          return false;
        case 'h':
          moved = ed.left();
          break;
        case 'l':
          moved = ed.right();
          break;
        case 'k':
          moved = ed.up(measure);
          break;
        case 'j':
          moved = ed.down(measure);
          break;
        default:
          break;
      }
      break;
    default:
      break;
  }

  if (edit == NoteEditor::Edit::Full) {
    s.sheetMessage = tr(STR_NOTE_FULL);
    s.noteDirty = true;
  } else if (edit == NoteEditor::Edit::Changed) {
    relayoutNoteLocked();
    s.noteDirty = true;
  } else if (moved) {
    s.note.scrollToCursor(static_cast<size_t>(noteSheetGeometry().visibleLines));
    s.noteDirty = true;
  }
  return false;
}

bool EpubReaderActivity::noteTouchLocked(const int x, const int y) {
  HintSession& s = *hints;
  if (x < 0) return false;  // a swipe: nothing destructive
  const NoteSheetGeometry g = noteSheetGeometry();
  if (g.cancel.contains(x, y)) return cancelNoteLocked();
  if (g.save.contains(x, y)) return saveNoteLocked();
  // A tap in the text area's rows (the right-hand slack included) moves the
  // cursor; anywhere else (page, header) is ignored.
  const UiRect area{g.text.x, g.text.y, static_cast<int16_t>(g.sheet.w - g.text.x), g.text.h};
  if (!area.contains(x, y)) return false;
  s.sheetMessage = nullptr;
  s.discardArmed = false;
  MeasureCtx ctx{&renderer, NOTE_FONT_ID};
  const size_t line = s.note.scrollTop() + static_cast<size_t>((y - g.text.y) / std::max(1, g.lineH));
  s.note.moveTo(line, x - g.text.x, TextMeasure{&ctx, &measureText});
  s.noteDirty = true;
  return false;
}
