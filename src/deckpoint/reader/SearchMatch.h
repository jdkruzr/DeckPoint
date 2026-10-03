#pragma once

// DECKPOINT: text matching for the reader's `/` search (pure, host-tested).
//
// Text and query are folded to a stream of "units": lower-case letters with
// Latin accents removed (é -> e, ß -> ss), digits, and other letters /
// ideographs as their codepoint. Everything else (spaces, punctuation, quotes,
// dashes, apostrophes) only separates words and is otherwise ignored, so
// "of the" matches the words "of" "the" across a line break, "don't" matches
// "don’t", and "well-known" matches "well-" / "known" on two lines.
//
// A match must start at the beginning of a word (after a separator); it may
// end anywhere, so "read" finds "reader" but not "bread". Ideographs (CJK)
// have no spaces and count as word starts on their own.

#include <cstddef>
#include <cstdint>

namespace deckpoint::reader {

// Folds one codepoint into at most 2 units. Returns the unit count; 0 with
// *separator set for a word separator, 0 with *separator clear for a mark to
// ignore (soft hyphen, combining accent, zero-width space).
uint8_t foldCodepoint(uint32_t cp, uint32_t out[2], bool* separator);

// True for a token that ends in a hyphen right after a letter ("won-"): at the
// end of a line that is a hyphenation break and the next token continues it.
bool endsWithHyphenBreak(const char* text, size_t len);

class SearchQuery {
 public:
  static constexpr size_t MAX_UNITS = 96;

  // Returns false when the text has nothing searchable (only punctuation).
  bool set(const char* utf8);
  const uint32_t* units() const { return unitBuf; }
  uint8_t length() const { return len; }

 private:
  uint32_t unitBuf[MAX_UNITS] = {};
  uint8_t len = 0;
};

// Streaming phrase matcher: feed a page's words one at a time in reading order;
// state carries across calls (and pages) until reset(). Token ids are the
// caller's (a running counter), echoed back in the hit.
class PhraseMatcher {
 public:
  struct Hit {
    uint32_t firstToken;  // token the match starts in
    uint32_t lastToken;   // token it ends in (the one just fed)
  };

  // `query` must outlive the matcher's use (not copied).
  void begin(const SearchQuery* query);
  // Drops partial matches (chapter boundary, backward page scans).
  void reset();
  // `continuesWord`: this token continues the previous one (hyphenation break),
  // so its first letter is not a word start. True when a match ended in this
  // token; hit() then says where (the earliest-starting one if several did).
  bool feed(const char* text, size_t len, uint32_t token, bool continuesWord);
  Hit hit() const { return lastHit; }

 private:
  static constexpr size_t RING = 128;  // > MAX_UNITS: start token of any live match
  static_assert(RING > SearchQuery::MAX_UNITS, "ring must cover a whole match");

  bool step(uint32_t unit, bool wordStart, uint32_t token);

  const SearchQuery* query = nullptr;
  // Shift-And state: bit j set = the last j+1 units equal the query's first j+1.
  uint64_t state[2] = {0, 0};
  uint32_t ring[RING] = {};  // token of each recent unit, by unit position
  uint32_t position = 0;     // units fed since reset()
  bool afterSeparator = true;
  Hit lastHit = {0, 0};
};

}  // namespace deckpoint::reader
