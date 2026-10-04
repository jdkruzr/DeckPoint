#pragma once

// DECKPOINT: state of a page overlay that works on the current page's words:
// the reader's `d` hint labels (EpubReaderHints.cpp) and the `v` / long-press
// highlight selection with its popups (EpubReaderSelection.cpp).
// Heap-allocated when it opens, freed when it closes.

#include <Epub/Page.h>

#include <memory>
#include <vector>

#include "HintMode.h"
#include "PageWords.h"
#include "SelectionSession.h"

namespace deckpoint::reader {

enum class HintPurpose : uint8_t {
  Lookup,  // `d`: the picked word is looked up
  Select,  // `v` / long-press: highlight selection
};

// What a Select session shows.
enum class SelectMode : uint8_t {
  Labels,         // hint labels pick the start or end word (keyboard)
  TouchEnd,       // anchor shown; a tap picks the end word (touch)
  Range,          // the selected range is shown inverted
  WordMenu,       // popup over a word: Look up / Highlight / Note
  HighlightMenu,  // popup over an existing highlight: Edit note / Delete / Look up
};

struct HintSession {
  // Owns the TextBlocks the word boxes point into.
  std::unique_ptr<Page> page;
  std::vector<WordBox> words;
  HintMatcher matcher;
  // The renderer holds the clean page from under the overlay (storeBwBuffer):
  // narrowing and cancelling restore it with one FAST refresh, no re-render.
  bool pageStored = false;
  // A page render asked for by the overlay itself (no stored page to
  // restore, or `?` help covered it): the overlay goes back on top of it.
  bool redrawAfterRender = false;
  // Any other page render erased the overlay; the session is dropped.
  bool stale = false;

  HintPurpose purpose = HintPurpose::Lookup;
  // Select sessions only.
  SelectMode mode = SelectMode::Labels;
  SelectionSession selection;
  ActionMenu menu;
  bool touchOrigin = false;  // opened by a long-press: taps pick and confirm
  bool withNote = false;     // "Note" chosen: the note editor follows the save
  int menuWord = -1;         // word the popup is about
  int highlight = -1;        // AnnotationList index for HighlightMenu
};

}  // namespace deckpoint::reader
