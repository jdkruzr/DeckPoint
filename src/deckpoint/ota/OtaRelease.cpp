#include "OtaRelease.h"

#include <cstdio>
#include <cstring>

namespace deckpoint_ota {

namespace {

// Parses a non-negative decimal at p, advancing it. False if there is no digit
// or the value is absurdly large.
bool parseNumber(const char*& p, int& out) {
  if (*p < '0' || *p > '9') return false;
  long value = 0;
  while (*p >= '0' && *p <= '9') {
    value = value * 10 + (*p - '0');
    if (value > 0xFFFFFF) return false;
    ++p;
  }
  out = static_cast<int>(value);
  return true;
}

}  // namespace

bool parseVersion(const char* text, Version& out) {
  if (text == nullptr) return false;
  const char* p = text;
  if (*p == 'v' || *p == 'V') ++p;
  Version v;
  if (!parseNumber(p, v.major) || *p++ != '.') return false;
  if (!parseNumber(p, v.minor) || *p++ != '.') return false;
  if (!parseNumber(p, v.patch)) return false;
  if (*p != '\0' && *p != '-' && *p != '+') return false;
  v.preRelease = strstr(p, "-rc") != nullptr || strstr(p, "-dev") != nullptr;
  out = v;
  return true;
}

bool isNewerRelease(const char* latestTag, const char* current) {
  Version latest;
  Version running;
  if (!parseVersion(latestTag, latest) || !parseVersion(current, running)) return false;
  if (latest.preRelease) return false;
  if (latest.major != running.major) return latest.major > running.major;
  if (latest.minor != running.minor) return latest.minor > running.minor;
  if (latest.patch != running.patch) return latest.patch > running.patch;
  return running.preRelease;
}

bool firmwareAssetName(const char* board, size_t boardLen, char* out, size_t outSize) {
  if (board == nullptr || boardLen == 0 || out == nullptr || outSize == 0) return false;
  const int n = snprintf(out, outSize, "deckpoint-%.*s.bin", static_cast<int>(boardLen), board);
  return n > 0 && static_cast<size_t>(n) < outSize;
}

}  // namespace deckpoint_ota
