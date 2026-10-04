// DECKPOINT: highlights in the reader. The book's AnnotationStore opens with
// the book; renderContents() asks planAnnotationMarks() for this page's
// underline / note-marker rectangles once (after the font prewarm) and draws
// them after every page->render: the B/W pass and each grayscale pass, so the
// underline stays solid black under anti-aliasing (in the gray planes a black
// pixel is "no gray here", which also clears AA fringe a descender leaves on
// the underline row).

#include <ChapterXPathResolver.h>
#include <Epub/Page.h>
#include <GfxRenderer.h>
#include <Logging.h>

#include <algorithm>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "activities/RenderLock.h"
#include "activities/reader/EpubReaderActivity.h"
#include "deckpoint/annotations/AnnotationGeometry.h"
#include "deckpoint/annotations/AnnotationSeed.h"

using deckpoint::annotations::AddResult;
using deckpoint::annotations::Annotation;
using deckpoint::annotations::AnnotationStore;
using deckpoint::annotations::Field;
using deckpoint::annotations::FIELD_COUNT;
using deckpoint::annotations::HighlightRange;
using deckpoint::annotations::MarkRect;
using deckpoint::annotations::MarkStyle;
using deckpoint::annotations::PageWord;

namespace {

// A full page runs ~100-150 words on 800x480, far fewer at 240x320.
constexpr size_t PAGE_WORDS_RESERVE = 160;

struct WordRef {
  const TextBlock* block;
  uint16_t index;
};

struct MeasureCtx {
  const GfxRenderer* renderer;
  int fontId;
  const std::vector<WordRef>* refs;
};

int measureWord(void* ctx, const size_t i) {
  const auto* c = static_cast<const MeasureCtx*>(ctx);
  const WordRef& r = (*c->refs)[i];
  return c->renderer->getTextAdvanceX(c->fontId, r.block->wordText(r.index), r.block->wordStyle(r.index),
                                      r.block->getBlockStyle().characterSpacing, BidiUtils::BidiBaseDir::AUTO,
                                      GfxRenderer::TextMeasureMode::Rendered);
}

MarkStyle markStyle(const int ascender) {
  MarkStyle s{};
  // Just under the baseline, where the font's own underline decoration sits.
  s.underlineOffset = static_cast<int16_t>(ascender + 2);
#if DECKPOINT_COMPACT_UI
  s.underlineThickness = 1;
  s.noteSize = 3;
#else
  s.underlineThickness = 2;
  s.noteSize = 4;
#endif
  // A small square at about x-height, just right of the last word.
  s.noteOffset = static_cast<int16_t>(ascender / 3);
  s.noteGap = 1;
  return s;
}

// Visits the page's words that carry a source offset, in reading order.
template <typename Fn>
void forEachSourceWord(const Page& page, const int ascender, Fn&& fn) {
  uint16_t lineIndex = 0;
  for (const auto& element : page.elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto* line = static_cast<const PageLine*>(element.get());
    const TextBlock* block = line->getBlock();
    if (!block || !block->valid()) continue;
    const int top = line->yPos + block->getRubyShift(ascender);
    for (uint16_t i = 0; i < block->wordCount(); i++) {
      if (block->wordHasVisibleOffset(i)) fn(*line, *block, i, top, lineIndex);
    }
    lineIndex++;
  }
}

}  // namespace

void EpubReaderActivity::openAnnotations() {
  annotationStore.reset();
  if (epub) annotationStore = AnnotationStore::open(epub->getPath());
}

void EpubReaderActivity::planAnnotationMarks(const Page& page, const int fontId, const int marginLeft,
                                             const int marginTop, std::vector<MarkRect>& out) {
  out.clear();
  if (!annotationStore || !annotationStore->hasHighlightsIn(currentSpineIndex)) return;
  annotationStore->placeChapter(epub, currentSpineIndex);

  // Transient, on annotated chapters only: ranges (12 B each), page words
  // (12 B + 8 B ref each, ~3 KB for a full 800x480 page).
  std::vector<HighlightRange> ranges;
  ranges.reserve(8);
  annotationStore->rangesFor(currentSpineIndex, ranges);
  if (ranges.empty()) return;

  std::vector<PageWord> words;
  std::vector<WordRef> refs;
  words.reserve(PAGE_WORDS_RESERVE);
  refs.reserve(PAGE_WORDS_RESERVE);
  const int ascender = renderer.getFontAscenderSize(fontId);
  forEachSourceWord(
      page, ascender,
      [&](const PageLine& line, const TextBlock& block, const uint16_t i, const int top, const uint16_t lineIndex) {
        const uint32_t start = block.wordVisibleOffset(i);
        words.push_back({start,
                         start + deckpoint::annotations::wordSourceLength(block.wordText(i), block.wordTextLen(i)),
                         static_cast<int16_t>(line.xPos + block.wordXpos(i) + marginLeft),
                         static_cast<int16_t>(top + marginTop), lineIndex});
        refs.push_back({&block, i});
      });

  MeasureCtx ctx{&renderer, fontId, &refs};
  deckpoint::annotations::planPageMarks(words.data(), words.size(), ranges.data(), ranges.size(), markStyle(ascender),
                                        &measureWord, &ctx, out);
}

void EpubReaderActivity::drawAnnotationMarks(const std::vector<MarkRect>& marks) const {
  for (const auto& r : marks) renderer.fillRect(r.x, r.y, r.w, r.h, true);
}

void EpubReaderActivity::annotationsTick() {
  deckpoint::annotations::SeedRequest request;
  if (!deckpoint::annotations::takeSeedRequest(request)) return;
  if (!epub || !section) {
    LOG_ERR("ANN", "Seed ignored: no open chapter");
    return;
  }
  if (!annotationStore) {
    LOG_ERR("ANN", "Seed ignored: annotations unavailable for this book");
    return;
  }

  std::string pos0 = std::move(request.pos0);
  std::string pos1 = std::move(request.pos1);
  std::string text;
  std::string chapter = currentChapterTitle();
  if (!request.testWords) {
    // An explicit range names its own chapter, wherever the reader happens to be.
    const int spine = deckpoint::annotations::spineFromXPointer(pos0);
    const int toc = spine >= 0 ? epub->getTocIndexForSpineIndex(spine) : -1;
    chapter = toc >= 0 ? epub->getTocItem(toc).title : std::string();
  }
  RenderLock lock;
  if (request.testWords) {
    // Words 3-8 of the current page, as a highlight of ours would select them.
    const auto page = section->loadPage(section->currentPage);
    if (!page) {
      LOG_ERR("ANN", "Seed: cannot load page %d", section->currentPage);
      return;
    }
    int n = 0;
    uint32_t start = 0;
    uint32_t end = 0;
    const int ascender = renderer.getFontAscenderSize(SETTINGS.getReaderFontId());
    forEachSourceWord(*page, ascender, [&](const PageLine&, const TextBlock& block, const uint16_t i, int, uint16_t) {
      n++;
      if (n < 3 || n > 8) return;
      const uint32_t s = block.wordVisibleOffset(i);
      if (n == 3) start = s;
      end = s + deckpoint::annotations::wordSourceLength(block.wordText(i), block.wordTextLen(i));
      if (!text.empty()) text.push_back(' ');
      if (text.size() + block.wordTextLen(i) <= deckpoint::annotations::MAX_TEXT_BYTES) {
        text.append(block.wordText(i), block.wordTextLen(i));
      }
    });
    if (n < 3) {
      LOG_ERR("ANN", "Seed: page has %d words", n);
      return;
    }
    using KOReaderXPointer::Bias;
    using KOReaderXPointer::Style;
    // Legacy spelling: what KOReader with crengine DOM < 20260812 writes.
    pos0 =
        ChapterXPathResolver::findXPathForVisibleTextOffset(epub, currentSpineIndex, start, Bias::Start, Style::Legacy);
    pos1 = ChapterXPathResolver::findXPathForVisibleTextOffset(epub, currentSpineIndex, end, Bias::End, Style::Legacy);
    if (pos0.empty() || pos1.empty()) {
      LOG_ERR("ANN", "Seed: no XPointer for offsets %u..%u", start, end);
      return;
    }
  }

  char now[20];
  AnnotationStore::now(now);
  std::string_view values[FIELD_COUNT];
  values[static_cast<size_t>(Field::Pos0)] = pos0;
  values[static_cast<size_t>(Field::Pos1)] = pos1;
  values[static_cast<size_t>(Field::Page)] = pos0;
  values[static_cast<size_t>(Field::Text)] = text;
  values[static_cast<size_t>(Field::Note)] = request.note;
  values[static_cast<size_t>(Field::Chapter)] = chapter;
  values[static_cast<size_t>(Field::Drawer)] = deckpoint::annotations::OWN_DRAWER;
  values[static_cast<size_t>(Field::Color)] = deckpoint::annotations::OWN_COLOR;
  values[static_cast<size_t>(Field::Datetime)] = now;
  Annotation annotation;
  if (!annotation.assign(values)) {
    LOG_ERR("ANN", "OOM: seed annotation");
    return;
  }
  const AddResult result = annotationStore->addAndSave(std::move(annotation));
  const bool stored = result == AddResult::Added || result == AddResult::Replaced;
  LOG_INF("ANN", "Seed %s: %s||%s \"%s\"", stored ? "stored" : "rejected", pos0.c_str(), pos1.c_str(), text.c_str());
  if (stored) requestUpdate();
}
