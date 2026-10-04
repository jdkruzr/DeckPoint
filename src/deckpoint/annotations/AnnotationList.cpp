#include "AnnotationList.h"

#include <string>

#include "AnnotationGeometry.h"

namespace deckpoint::annotations {

AddResult AnnotationList::add(Annotation&& annotation) {
  if (readOnlyFlag) return AddResult::ReadOnly;
  if (annotation.get(Field::Text).size() > MAX_TEXT_BYTES || annotation.get(Field::Note).size() > MAX_NOTE_BYTES) {
    return AddResult::TooLong;
  }
  return adopt(std::move(annotation));
}

AddResult AnnotationList::adopt(Annotation&& annotation) {
  if (readOnlyFlag) return AddResult::ReadOnly;
  const std::string key = annotationKey(annotation);
  if (key.empty()) return AddResult::NotPlaceable;
  annotation.spineIndex = static_cast<int16_t>(spineFromXPointer(annotation.get(Field::Pos0)));
  annotation.endSpineIndex = static_cast<int16_t>(spineFromXPointer(annotation.get(Field::Pos1)));
  annotation.placement = Placement::Unresolved;
  if (annotation.isHighlight() && (annotation.spineIndex < 0 || annotation.endSpineIndex < annotation.spineIndex)) {
    annotation.placement = Placement::Failed;  // not an EPUB range, or ends before it starts: kept, never drawn
  }

  const int existing = find(key);
  const size_t freed = existing >= 0 ? items[static_cast<size_t>(existing)].heapBytes() : 0;
  if (blobTotal - freed + annotation.heapBytes() > blobBudget) return AddResult::OverBudget;
  if (existing >= 0) {
    blobTotal = blobTotal - freed + annotation.heapBytes();
    items[static_cast<size_t>(existing)] = std::move(annotation);
    return AddResult::Replaced;
  }
  if (items.size() >= MAX_ANNOTATIONS) return AddResult::TooMany;
  blobTotal += annotation.heapBytes();
  items.push_back(std::move(annotation));
  return AddResult::Added;
}

bool AnnotationList::tombstone(const size_t i, const std::string_view now) {
  if (readOnlyFlag || i >= items.size()) return false;
  Annotation& a = items[i];
  const size_t before = a.heapBytes();
  if (!a.set(Field::DatetimeUpdated, now)) return false;
  blobTotal = blobTotal - before + a.heapBytes();
  a.deleted = true;
  return true;
}

AddResult AnnotationList::setNote(const size_t i, const std::string_view note, const std::string_view now) {
  if (readOnlyFlag) return AddResult::ReadOnly;
  if (i >= items.size() || items[i].deleted) return AddResult::NotPlaceable;
  Annotation& a = items[i];
  if (note.size() > MAX_NOTE_BYTES && note.size() > a.get(Field::Note).size()) return AddResult::TooLong;
  std::string_view values[FIELD_COUNT];
  for (size_t f = 0; f < FIELD_COUNT; f++) values[f] = a.get(static_cast<Field>(f));
  values[static_cast<size_t>(Field::Note)] = note;
  values[static_cast<size_t>(Field::DatetimeUpdated)] = now;
  size_t fresh = 0;
  for (const auto& v : values) fresh += v.size() + 1;
  const size_t before = a.heapBytes();
  if (blobTotal - before + fresh > blobBudget) return AddResult::OverBudget;
  // One rebuild for both fields; the views into the old block stay valid until it is replaced.
  if (!a.assign(values)) return AddResult::OverBudget;  // OOM
  blobTotal = blobTotal - before + a.heapBytes();
  return AddResult::Replaced;
}

int AnnotationList::find(const std::string_view key) const {
  const size_t sep = key.find("||");
  for (size_t i = 0; i < items.size(); i++) {
    const Annotation& a = items[i];
    if (a.isHighlight()) {
      if (sep != std::string_view::npos && a.get(Field::Pos0) == key.substr(0, sep) &&
          a.get(Field::Pos1) == key.substr(sep + 2)) {
        return static_cast<int>(i);
      }
    } else if (key.size() > 9 && key.substr(0, 9) == "BOOKMARK|" && a.get(Field::Page) == key.substr(9)) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

bool AnnotationList::hasHighlightsIn(const int spineIndex) const {
  for (const auto& a : items) {
    if (a.spans(spineIndex) && a.isHighlight() && a.placement != Placement::Failed) return true;
  }
  return false;
}

AnnotationList::PlaceCount AnnotationList::placeChapter(const int spineIndex, const ResolveXPointer resolve,
                                                        void* ctx) {
  PlaceCount count;
  for (auto& a : items) {
    if (a.placement != Placement::Unresolved || !a.isHighlight() || !a.spans(spineIndex)) continue;
    // Copies: the resolver takes std::string.
    const std::string pos0(a.get(Field::Pos0));
    const std::string pos1(a.get(Field::Pos1));
    uint32_t start = 0;
    uint32_t end = 0;
    bool ok = resolve(ctx, a.spineIndex, pos0, start) && resolve(ctx, a.endSpineIndex, pos1, end);
    if (ok && a.spineIndex == a.endSpineIndex) ok = end > start;
    if (ok) {
      a.startOffset = start;
      a.endOffset = end;
      a.placement = Placement::Resolved;
      count.placed++;
    } else {
      a.placement = Placement::Failed;
      count.failed++;
    }
  }
  return count;
}

void AnnotationList::rangesFor(const int spineIndex, std::vector<HighlightRange>& out) const {
  out.clear();
  for (const auto& a : items) {
    HighlightRange r{};
    if (!a.rangeIn(spineIndex, r.start, r.end) || r.end <= r.start) continue;
    r.hasNote = spineIndex == a.endSpineIndex && a.has(Field::Note);
    out.push_back(r);
  }
}

int AnnotationList::highlightAt(const int spineIndex, const uint32_t offset) const {
  for (size_t i = items.size(); i-- > 0;) {
    if (items[i].covers(spineIndex, offset)) return static_cast<int>(i);
  }
  return -1;
}

void AnnotationList::clear() {
  items.clear();
  blobTotal = 0;
  readOnlyFlag = false;
}

}  // namespace deckpoint::annotations
