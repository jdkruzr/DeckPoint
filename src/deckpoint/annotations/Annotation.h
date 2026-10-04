#pragma once

// DECKPOINT: one KOReader-shaped annotation (highlight, optionally with a note),
// as AnnotationSync stores it in its remote JSON map. Field names and meanings
// follow KOReader: pos0/pos1 are XPointers, pos1 exclusive (just past the last
// character); page == pos0 for EPUB; datetime strings are "YYYY-MM-DD HH:MM:SS"
// local time. Bookmark entries ("BOOKMARK|<page>" keys: page, no pos0/pos1) are
// kept so a rewrite does not drop them, but never drawn.
//
// Memory: every string lives in one NUL-separated heap block per annotation
// (PSRAM when the board has it), so an entry costs sizeof(Annotation) (~44 B)
// of internal RAM plus a single allocation, instead of ten std::string headers
// and up to ten small blocks.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace deckpoint::annotations {

enum class Field : uint8_t {
  Pos0,
  Pos1,
  Text,
  Note,
  Chapter,
  Page,
  Drawer,
  Color,
  Datetime,
  DatetimeUpdated,
  COUNT,
};
constexpr size_t FIELD_COUNT = static_cast<size_t>(Field::COUNT);

// KOReader's JSON key for a field.
const char* fieldName(Field field);
// Inverse of fieldName; false for keys this model does not keep.
bool fieldFromName(std::string_view name, Field& out);

// Our own writes (new highlights, note edits). Loaded entries may be longer, up
// to what the JSON reader can hold (StreamingJsonParser::TOKEN_BUF_SIZE - 1).
constexpr size_t MAX_TEXT_BYTES = 1024;
// One under StreamingJsonParser::TOKEN_BUF_SIZE so our own notes read back.
constexpr size_t MAX_NOTE_BYTES = 2047;
constexpr size_t MAX_DATETIME_BYTES = 19;  // "YYYY-MM-DD HH:MM:SS"

// What DeckPoint writes for highlights it creates. KOReader shows "underscore"
// as an underline; incoming entries keep their own drawer/color.
constexpr const char* OWN_DRAWER = "underscore";
constexpr const char* OWN_COLOR = "gray";

enum class Placement : uint8_t {
  Unresolved,  // not looked up yet
  Resolved,    // startOffset/endOffset valid for spineIndex
  Failed,      // XPointers did not resolve in this book; kept, never drawn
};

class Annotation {
 public:
  Annotation() = default;
  Annotation(Annotation&&) noexcept = default;
  Annotation& operator=(Annotation&&) noexcept = default;
  Annotation(const Annotation&) = delete;
  Annotation& operator=(const Annotation&) = delete;

  // Empty view when unset. The view is NUL-terminated (safe as a C string via
  // .data()) and valid until the next assign().
  std::string_view get(Field field) const;
  bool has(Field field) const { return !get(field).empty(); }

  // Replaces every string field at once (one allocation). False on OOM or when
  // the total exceeds the 16-bit offset range; the annotation is unchanged then.
  bool assign(const std::string_view (&values)[FIELD_COUNT]);
  // Replaces one field (rebuilds the block). Same failure rules as assign().
  bool set(Field field, std::string_view value);

  // A highlight (pos0 and pos1 set), as opposed to a bookmark entry.
  bool isHighlight() const { return has(Field::Pos0) && has(Field::Pos1); }
  // Heap bytes held by the string block.
  size_t heapBytes() const { return blobSize; }

  // KOReader page number (layout-dependent; preserved, never interpreted).
  int32_t pageno = 0;
  bool hasPageno = false;
  // "page" was a JSON number (non-EPUB documents); written back unquoted.
  bool pageIsNumber = false;
  // Tombstone: AnnotationSync's way to propagate a delete. Kept and written.
  bool deleted = false;

  // Local-only state, kept in the <md5>.local.json sidecar and never written
  // into the AnnotationSync shape. undated: the latest stamp (datetime_updated,
  // else datetime) was taken while the clock was not current
  // (trustedtime::isCurrent); sync re-stamps it before merging. localOnly: a
  // dev seed (CMD:ANNOTATE*), never uploaded and never merged.
  bool undated = false;
  bool localOnly = false;

  // Placement cache (not serialized): the highlight as visible-text offsets in
  // spineIndex, [startOffset, endOffset). spineIndex is parsed from pos0 at
  // load (-1: not an EPUB XPointer).
  int16_t spineIndex = -1;
  Placement placement = Placement::Unresolved;
  uint32_t startOffset = 0;
  uint32_t endOffset = 0;

 private:
  struct BlobFree {
    void operator()(char* p) const;
  };
  std::unique_ptr<char[], BlobFree> blob;
  uint16_t offsets[FIELD_COUNT] = {};  // start of each field's string in blob
  uint16_t blobSize = 0;
};

// AnnotationSync's map key: pos0 .. "||" .. pos1 for highlights,
// "BOOKMARK|" .. page for bookmarks. Empty when neither applies.
std::string annotationKey(const Annotation& annotation);

}  // namespace deckpoint::annotations
