#pragma once

// DECKPOINT: per-book reader marks (m<a-z> sets, '<a-z> jumps). A mark keeps
// the same anchor a bookmark does: spine index plus the page's visible-text
// offset, which survives re-pagination (font, margins, orientation); the page
// number is only the fallback for pages without an offset.

#include <cstddef>
#include <cstdint>
#include <string>

namespace deckpoint::reader {

struct MarkPosition {
  uint16_t spineIndex = 0;
  uint16_t page = 0;
  bool hasVisibleTextOffset = false;
  uint32_t visibleTextOffset = 0;
};

struct MarkTable {
  static constexpr int COUNT = 26;  // a..z
  MarkPosition marks[COUNT] = {};
  uint32_t setMask = 0;  // bit i: marks[i] is set

  static int indexFor(char letter) { return letter >= 'a' && letter <= 'z' ? letter - 'a' : -1; }
  bool isSet(char letter) const;
  const MarkPosition* get(char letter) const;
  void set(char letter, const MarkPosition& position);
};

// marks.bin: 'M' 'K' version, u32 set mask, then COUNT fixed 9-byte records
// (u16 spine, u16 page, u8 flags, u32 offset), all little-endian.
namespace marks_codec {
constexpr uint8_t VERSION = 1;
constexpr size_t HEADER_SIZE = 3 + 4;
constexpr size_t RECORD_SIZE = 9;
constexpr size_t FILE_SIZE = HEADER_SIZE + RECORD_SIZE * MarkTable::COUNT;

void encode(const MarkTable& table, uint8_t (&out)[FILE_SIZE]);
// False (table untouched) on a size, magic or version mismatch.
bool decode(const uint8_t* data, size_t size, MarkTable& table);
}  // namespace marks_codec

// SD persistence in the book's cache directory (<cachePath>/marks.bin).
// A missing file loads as an empty table and returns false.
bool loadMarks(const std::string& cachePath, MarkTable& table);
bool saveMarks(const std::string& cachePath, const MarkTable& table);

}  // namespace deckpoint::reader
