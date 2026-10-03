#pragma once

// DECKPOINT: state of the reader's `/` search (EpubReaderSearch.cpp).
// A SearchSession lives only while a search runs (heap, ~1.1 KB plus a Section
// for a chapter other than the open one); SearchMark is the matched words'
// inversion on the page that was jumped to.

#include <Epub/Section.h>

#include <cstdint>
#include <memory>

#include "SearchMatch.h"

namespace deckpoint::reader {

struct SearchSession {
  SearchQuery query;
  PhraseMatcher matcher;
  bool forward = true;
  // Started from the `/` band (it stays open for an edit-and-retry on a miss).
  bool fromPrompt = false;

  // Where the search started: page, and the match it continues from there
  // (page-local start token). Forward: matches after it (-1 = the whole
  // page). Backward: matches before it (NO_LIMIT = the whole page).
  static constexpr int32_t NO_LIMIT = 0x7FFFFFFF;
  int originSpine = 0;
  int originPage = 0;
  int32_t originToken = -1;

  // Next page to scan. LAST_PAGE: the chapter's last page (backward).
  static constexpr int LAST_PAGE = -1;
  int spine = 0;
  int page = 0;
  bool wrapped = false;

  // Running token id (all pages scanned), the first id on the page being
  // scanned and on the one before it (a phrase can start on that one).
  uint32_t nextToken = 0;
  uint32_t pageFirstToken = 0;
  uint32_t prevFirstToken = 0;
  int prevSpine = -1;
  int prevPage = -1;
  // The last word scanned ended a line with a hyphenation break.
  bool hyphenCarry = false;

  // Layout of a chapter other than the open one (the open one is the
  // reader's own section). cacheLoaded: complete .bin found on SD.
  std::unique_ptr<Section> section;
  int sectionSpine = -1;
  bool cacheLoaded = false;

  unsigned long startMs = 0;
  unsigned long lastPaintMs = 0;
  int paintedSpine = -1;
  // Consecutive ticks the build heap gate said no.
  uint16_t heapWaits = 0;
};

// The matched words, inverted over the page they are on until the next key or
// page change. Tokens are page-local (PageWords' WordBox::token).
struct SearchMark {
  bool active = false;
  bool pageStored = false;  // the renderer holds the clean page (storeBwBuffer)
  bool wrappedToast = false;
  int spine = -1;
  int page = -1;
  uint16_t firstToken = 0;
  uint16_t lastToken = 0;
};

}  // namespace deckpoint::reader
