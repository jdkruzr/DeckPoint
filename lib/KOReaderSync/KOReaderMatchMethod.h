#pragma once

#include <cstdint>

// Document matching method for KOReader sync
enum class DocumentMatchMethod : uint8_t {
  FILENAME = 0,  // Match by filename (any file with the same name shares progress)
  BINARY = 1,    // Match by partial MD5 of file content (what KOReader uses by default)
};

namespace kosync {

// koreader.json config version from which a stored matchMethod is kept as is.
constexpr uint8_t MATCH_METHOD_BINARY_DEFAULT_VERSION = 3;

// DECKPOINT: document matching is always Binary (partial MD5 of the content).
// Filename matching silently pairs unrelated books that share a file name, so
// any stored value, including an explicit Filename choice, loads as Binary.
inline DocumentMatchMethod loadMatchMethod(int /*stored*/, uint8_t /*cfgVersion*/) {
  return DocumentMatchMethod::BINARY;
}

}  // namespace kosync
