#pragma once

#include <cstddef>

// DECKPOINT: pure helpers for OTA release selection, shared by OtaUpdater and
// the host tests (no Arduino/ESP-IDF dependencies).
//
// Version strings look like "<major>.<minor>.<patch>[-<suffix>]", optionally
// prefixed with 'v' (release tags). The suffix is a board name ("-tdeckpro"),
// a pre-release marker ("-rc+abc1234", "-dev-<branch>-<sha>") or both; only
// the pre-release markers affect ordering.
namespace deckpoint_ota {

// DeckPoint firmware updates come from DeckPoint's own releases, never
// upstream CrossPoint's (whose X4 images must not reach a T-Deck).
inline constexpr char LATEST_RELEASE_URL[] = "https://api.github.com/repos/jdkruzr/DeckPoint/releases/latest";

struct Version {
  int major = 0;
  int minor = 0;
  int patch = 0;
  bool preRelease = false;  // "-rc" or "-dev" follows the numeric triple
};

// Parses the leading numeric triple. False when it is missing or malformed.
bool parseVersion(const char* text, Version& out);

// True when `latestTag` should replace the running `current` version: higher
// triple, or the same triple while the running build is a pre-release (an rc or
// dev build of X is older than release X). False if either fails to parse or
// the latest is itself a pre-release.
bool isNewerRelease(const char* latestTag, const char* current);

// Writes "deckpoint-<board>.bin". The board name need not be null-terminated
// (FirmwareBoardTag hands out a slice), hence the length. False on overflow or
// an empty name.
bool firmwareAssetName(const char* board, size_t boardLen, char* out, size_t outSize);

}  // namespace deckpoint_ota
