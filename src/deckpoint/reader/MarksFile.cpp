#include <HalStorage.h>
#include <Logging.h>

#include "Marks.h"

// DECKPOINT: marks.bin persistence beside the book's progress.bin.

namespace deckpoint::reader {

namespace {
std::string marksPath(const std::string& cachePath) { return cachePath + "/marks.bin"; }
}  // namespace

bool loadMarks(const std::string& cachePath, MarkTable& table) {
  table = MarkTable{};
  const std::string path = marksPath(cachePath);
  if (!Storage.exists(path.c_str())) return false;
  HalFile f;
  if (!Storage.openFileForRead("MRK", path, f)) return false;
  // One byte of slack so an oversized file is rejected instead of truncated.
  uint8_t data[marks_codec::FILE_SIZE + 1];
  const int size = f.read(data, sizeof(data));
  if (size <= 0 || !marks_codec::decode(data, static_cast<size_t>(size), table)) {
    LOG_ERR("MRK", "Ignoring unreadable marks file (%d bytes)", size);
    return false;
  }
  LOG_DBG("MRK", "Loaded marks, mask 0x%08lx", static_cast<unsigned long>(table.setMask));
  return true;
}

bool saveMarks(const std::string& cachePath, const MarkTable& table) {
  uint8_t data[marks_codec::FILE_SIZE];
  marks_codec::encode(table, data);
  HalFile f;
  if (!Storage.openFileForWrite("MRK", marksPath(cachePath), f)) return false;
  if (f.write(data, sizeof(data)) != sizeof(data)) {
    LOG_ERR("MRK", "Failed to write marks");
    return false;
  }
  return true;
}

}  // namespace deckpoint::reader
