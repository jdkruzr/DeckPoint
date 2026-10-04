#pragma once

// DECKPOINT: the selectable words of a laid-out reader page with their screen
// boxes. Shared by DictionaryWordSelectActivity (cursor word picking) and the
// reader's `d` hint mode; round 2 highlights / link hints reuse it.

#include <EpdFontFamily.h>

#include <cstdint>
#include <vector>

#include "WordToken.h"

class GfxRenderer;
class Page;

namespace deckpoint::reader {

// Screen box of one selectable word. `text` points into the Page's TextBlock
// arena (NUL-terminated): valid only while that Page is alive.
struct WordBox {
  int16_t x;
  int16_t y;  // top of the line
  int16_t width;
  uint16_t row;    // index among the page's lines that have selectable words
  uint16_t token;  // index among all the page's words (punctuation too), in page order
  const char* text;
  EpdFontFamily::Style style;
  // Zero-based visible codepoint offset of the word's first source character in the
  // spine item (TextBlock::wordVisibleOffset): the anchor for highlights and XPointers.
  uint32_t visibleOffset;
};

// Fills `out` (cleared first) with the page's selectable words in reading
// order (token numbering: every word of every valid TextBlock line counts), at the offsets the page was rendered with.
// Widths are measured with `fontId`; SD-card fonts get the page's codepoints merged in first so the measuring stays on
// the in-RAM advance table. Returns the row count. Callers that share the renderer with a render task hold its
// RenderLock.
uint16_t extractPageWords(const GfxRenderer& renderer, const Page& page, int fontId, int marginLeft, int marginTop,
                          std::vector<WordBox>& out);

}  // namespace deckpoint::reader
