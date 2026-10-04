#include "Annotation.h"

#include <cstdlib>
#include <cstring>

#if defined(ESP_PLATFORM) && defined(BOARD_HAS_PSRAM)
#include <esp_heap_caps.h>
#endif

namespace deckpoint::annotations {

namespace {

constexpr const char* FIELD_NAMES[FIELD_COUNT] = {
    "pos0", "pos1", "text", "note", "chapter", "page", "drawer", "color", "datetime", "datetime_updated",
};

// String blocks go to PSRAM where there is some: they are read once per chapter
// placement and per save, never on the per-pixel path.
char* allocBlob(const size_t bytes) {
#if defined(ESP_PLATFORM) && defined(BOARD_HAS_PSRAM)
  if (void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) return static_cast<char*>(p);
#endif
  return static_cast<char*>(std::malloc(bytes));
}

}  // namespace

void Annotation::BlobFree::operator()(char* p) const { std::free(p); }

const char* fieldName(const Field field) {
  const auto i = static_cast<size_t>(field);
  return i < FIELD_COUNT ? FIELD_NAMES[i] : "";
}

bool fieldFromName(const std::string_view name, Field& out) {
  for (size_t i = 0; i < FIELD_COUNT; i++) {
    if (name == FIELD_NAMES[i]) {
      out = static_cast<Field>(i);
      return true;
    }
  }
  return false;
}

std::string_view Annotation::get(const Field field) const {
  const auto i = static_cast<size_t>(field);
  if (!blob || i >= FIELD_COUNT) return {};
  const char* s = blob.get() + offsets[i];
  return {s, strlen(s)};
}

bool Annotation::assign(const std::string_view (&values)[FIELD_COUNT]) {
  size_t total = 0;
  for (const auto& v : values) total += v.size() + 1;
  if (total > UINT16_MAX) return false;
  std::unique_ptr<char[], BlobFree> fresh(allocBlob(total));
  if (!fresh) return false;
  uint16_t freshOffsets[FIELD_COUNT];
  size_t pos = 0;
  for (size_t i = 0; i < FIELD_COUNT; i++) {
    freshOffsets[i] = static_cast<uint16_t>(pos);
    if (!values[i].empty()) memcpy(fresh.get() + pos, values[i].data(), values[i].size());
    pos += values[i].size();
    fresh[pos++] = '\0';
  }
  blob = std::move(fresh);
  memcpy(offsets, freshOffsets, sizeof(offsets));
  blobSize = static_cast<uint16_t>(total);
  return true;
}

bool Annotation::set(const Field field, const std::string_view value) {
  const auto index = static_cast<size_t>(field);
  if (index >= FIELD_COUNT) return false;
  std::string_view values[FIELD_COUNT];
  for (size_t i = 0; i < FIELD_COUNT; i++) values[i] = get(static_cast<Field>(i));
  values[index] = value;
  // `value` may point into the current block; assign() copies before freeing it.
  return assign(values);
}

std::string annotationKey(const Annotation& annotation) {
  std::string key;
  if (annotation.isHighlight()) {
    const auto p0 = annotation.get(Field::Pos0);
    const auto p1 = annotation.get(Field::Pos1);
    key.reserve(p0.size() + 2 + p1.size());
    key.append(p0).append("||").append(p1);
  } else if (annotation.has(Field::Page)) {
    const auto page = annotation.get(Field::Page);
    key.reserve(9 + page.size());
    key.append("BOOKMARK|").append(page);
  }
  return key;
}

}  // namespace deckpoint::annotations
