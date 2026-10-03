#pragma once

// DECKPOINT: state of the reader's `d` hint mode while its labels are up
// (EpubReaderHints.cpp). Heap-allocated on `d`, freed when the hints close.

#include <Epub/Page.h>

#include <memory>
#include <vector>

#include "HintMode.h"
#include "PageWords.h"

namespace deckpoint::reader {

struct HintSession {
  // Owns the TextBlocks the word boxes point into.
  std::unique_ptr<Page> page;
  std::vector<WordBox> words;
  HintMatcher matcher;
  // The renderer holds the clean page from under the labels (storeBwBuffer):
  // narrowing and cancelling restore it with one FAST refresh, no re-render.
  bool pageStored = false;
  // A page render asked for by the hints themselves (no stored page to
  // restore): the labels go back on top of it.
  bool redrawAfterRender = false;
  // Any other page render erased the labels; the session is dropped.
  bool stale = false;
};

}  // namespace deckpoint::reader
