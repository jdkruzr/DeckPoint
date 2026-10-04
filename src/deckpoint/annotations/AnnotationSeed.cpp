#include "AnnotationSeed.h"

#include <Arduino.h>
#include <Memory.h>

#include <cstring>
#include <memory>

namespace deckpoint::annotations {

namespace {

// Pending request (main loop task writes, the same task's activity loop takes
// it); allocated only while one is pending.
std::unique_ptr<SeedRequest> pending;

constexpr unsigned long WIPE_EXPIRY_MS = 3000;
unsigned long wipeRequestedAt = 0;
bool wipePending = false;

std::string unescapeNewlines(const char* s) {
  std::string out;
  out.reserve(strlen(s));
  for (; *s; s++) {
    if (s[0] == '\\' && s[1] == 'n') {
      out.push_back('\n');
      s++;
    } else {
      out.push_back(*s);
    }
  }
  return out;
}

}  // namespace

bool requestSeed(const char* command) {
  constexpr const char* TEST = "ANNOTATE_TEST";
  constexpr const char* RANGE = "ANNOTATE:";
  auto request = makeUniqueNoThrow<SeedRequest>();
  if (!request) return false;
  if (strncmp(command, TEST, strlen(TEST)) == 0) {
    const char* rest = command + strlen(TEST);
    if (*rest != '\0' && *rest != ':') return false;
    request->testWords = true;
    if (*rest == ':') request->note = unescapeNewlines(rest + 1);
  } else if (strncmp(command, RANGE, strlen(RANGE)) == 0) {
    const std::string arg(command + strlen(RANGE));
    const size_t sep = arg.find("||");
    if (sep == std::string::npos || sep == 0) return false;
    const size_t sep2 = arg.find("||", sep + 2);
    request->pos0 = arg.substr(0, sep);
    request->pos1 = arg.substr(sep + 2, sep2 == std::string::npos ? std::string::npos : sep2 - sep - 2);
    if (sep2 != std::string::npos) request->note = unescapeNewlines(arg.c_str() + sep2 + 2);
    if (request->pos1.empty()) return false;
  } else {
    return false;
  }
  pending = std::move(request);
  return true;
}

void requestWipe() {
  wipeRequestedAt = millis();
  wipePending = true;
}

bool takeWipeRequest() {
  if (!wipePending) return false;
  wipePending = false;
  return millis() - wipeRequestedAt <= WIPE_EXPIRY_MS;
}

bool takeSeedRequest(SeedRequest& out) {
  if (!pending) return false;
  out = std::move(*pending);
  pending.reset();
  return true;
}

}  // namespace deckpoint::annotations
