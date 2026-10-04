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
  annotation.placement = Placement::Unresolved;

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
    if (a.spineIndex == spineIndex && !a.deleted && a.isHighlight() && a.placement != Placement::Failed) return true;
  }
  return false;
}

void AnnotationList::clear() {
  items.clear();
  blobTotal = 0;
  readOnlyFlag = false;
}

}  // namespace deckpoint::annotations
