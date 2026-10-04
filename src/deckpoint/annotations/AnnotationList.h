#pragma once

// DECKPOINT: one book's annotations in memory (host-tested). Caps and the RAM
// budget live here; SD I/O and placement are AnnotationStore's.

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "Annotation.h"

namespace deckpoint::annotations {

enum class AddResult : uint8_t {
  Added,
  Replaced,      // same key: the entry was updated in place
  ReadOnly,      // the list could not be loaded completely; it is never saved
  TooMany,       // MAX_ANNOTATIONS reached
  TooLong,       // text or note over its cap (our own writes only)
  OverBudget,    // string blocks would exceed the RAM budget
  NotPlaceable,  // neither a highlight nor a bookmark (no key)
};

class AnnotationList {
 public:
  static constexpr size_t MAX_ANNOTATIONS = 500;
  // String-block bytes held in RAM. PSRAM boards keep the blocks there, so the
  // count cap binds first; internal-RAM-only boards (ESP32-C3) stop at ~60-100
  // typical highlights (~250-400 B each) rather than eat the reader's heap.
#if defined(BOARD_HAS_PSRAM)
  static constexpr size_t DEFAULT_BLOB_BUDGET = 1024 * 1024;
#else
  static constexpr size_t DEFAULT_BLOB_BUDGET = 24 * 1024;
#endif

  explicit AnnotationList(size_t blobBudget = DEFAULT_BLOB_BUDGET) : blobBudget(blobBudget) {}

  size_t size() const { return items.size(); }
  bool empty() const { return items.empty(); }
  const Annotation& operator[](const size_t i) const { return items[i]; }
  Annotation& operator[](const size_t i) { return items[i]; }
  void reserve(const size_t n) { items.reserve(n < MAX_ANNOTATIONS ? n : MAX_ANNOTATIONS); }

  // A new or edited annotation of ours: enforces MAX_TEXT_BYTES / MAX_NOTE_BYTES
  // on top of adopt()'s checks.
  AddResult add(Annotation&& annotation);
  // An annotation read from a file (no text/note cap: the reader already
  // bounds every string). Replaces an entry with the same key.
  AddResult adopt(Annotation&& annotation);

  // Marks entry i deleted (AnnotationSync tombstone) with datetime_updated =
  // `now`. False when read-only, out of range or on OOM (entry unchanged).
  bool tombstone(size_t i, std::string_view now);
  // Sets (empty: removes) entry i's note, stamping datetime_updated = `now`.
  // TooLong past MAX_NOTE_BYTES, unless the note is not longer than the one it
  // replaces (an imported longer note can still be shortened); ReadOnly when
  // the list is, NotPlaceable for a bad index or a deleted entry.
  AddResult setNote(size_t i, std::string_view note, std::string_view now);

  // Index of the entry with this AnnotationSync key, or -1.
  int find(std::string_view key) const;
  // True when some live highlight sits in this spine item.
  bool hasHighlightsIn(int spineIndex) const;

  // Set when something in the file could not be held (oversized value, cap,
  // budget, malformed entry): saving would lose it, so the list stays read-only.
  bool readOnly() const { return readOnlyFlag; }
  void markReadOnly() { readOnlyFlag = true; }

  size_t blobBytes() const { return blobTotal; }
  size_t blobBudgetBytes() const { return blobBudget; }
  void clear();

 private:
  std::vector<Annotation> items;
  size_t blobBudget;
  size_t blobTotal = 0;
  bool readOnlyFlag = false;
};

}  // namespace deckpoint::annotations
