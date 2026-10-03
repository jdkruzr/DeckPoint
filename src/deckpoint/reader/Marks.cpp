#include "Marks.h"

// DECKPOINT: pure mark table + marks.bin codec (host-tested); SD access lives
// in MarksFile.cpp.

namespace deckpoint::reader {

namespace {
constexpr uint8_t MAGIC0 = 'M';
constexpr uint8_t MAGIC1 = 'K';
constexpr uint8_t FLAG_HAS_OFFSET = 0x01;

void putU16(uint8_t* p, const uint16_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>(v >> 8);
}

void putU32(uint8_t* p, const uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = static_cast<uint8_t>((v >> (8 * i)) & 0xFF);
}

uint16_t getU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

uint32_t getU32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

constexpr uint32_t ALL_MARKS_MASK = (1UL << MarkTable::COUNT) - 1;
}  // namespace

bool MarkTable::isSet(const char letter) const {
  const int i = indexFor(letter);
  return i >= 0 && (setMask & (1UL << i)) != 0;
}

const MarkPosition* MarkTable::get(const char letter) const { return isSet(letter) ? &marks[indexFor(letter)] : nullptr; }

void MarkTable::set(const char letter, const MarkPosition& position) {
  const int i = indexFor(letter);
  if (i < 0) return;
  marks[i] = position;
  setMask |= 1UL << i;
}

namespace marks_codec {

void encode(const MarkTable& table, uint8_t (&out)[FILE_SIZE]) {
  out[0] = MAGIC0;
  out[1] = MAGIC1;
  out[2] = VERSION;
  putU32(out + 3, table.setMask & ALL_MARKS_MASK);
  for (int i = 0; i < MarkTable::COUNT; i++) {
    uint8_t* r = out + HEADER_SIZE + i * RECORD_SIZE;
    const MarkPosition& m = table.marks[i];
    putU16(r, m.spineIndex);
    putU16(r + 2, m.page);
    r[4] = m.hasVisibleTextOffset ? FLAG_HAS_OFFSET : 0;
    putU32(r + 5, m.hasVisibleTextOffset ? m.visibleTextOffset : 0);
  }
}

bool decode(const uint8_t* data, const size_t size, MarkTable& table) {
  if (data == nullptr || size != FILE_SIZE || data[0] != MAGIC0 || data[1] != MAGIC1 || data[2] != VERSION) {
    return false;
  }
  MarkTable decoded;
  decoded.setMask = getU32(data + 3) & ALL_MARKS_MASK;
  for (int i = 0; i < MarkTable::COUNT; i++) {
    const uint8_t* r = data + HEADER_SIZE + i * RECORD_SIZE;
    MarkPosition& m = decoded.marks[i];
    m.spineIndex = getU16(r);
    m.page = getU16(r + 2);
    m.hasVisibleTextOffset = (r[4] & FLAG_HAS_OFFSET) != 0;
    m.visibleTextOffset = m.hasVisibleTextOffset ? getU32(r + 5) : 0;
  }
  table = decoded;
  return true;
}

}  // namespace marks_codec

}  // namespace deckpoint::reader
