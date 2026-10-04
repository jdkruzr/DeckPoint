#pragma once

// DECKPOINT: the decisions of one book's annotation sync, kept free of I/O so
// they are host-tested (test/annotation_sync). AnnotationSync.cpp runs them:
//   GET <folder>/<md5>.json -> remoteAfterGet
//   (merge)
//   PUT with If-Match / If-None-Match: * -> afterPut, until Done or a failure.

#include <cstddef>
#include <cstdint>

#include "WebDavProtocol.h"

namespace annotationsync {

// What the remote holds after the GET.
enum class Remote : uint8_t {
  Present,        // 200: a file to merge (its ETag guards the PUT)
  Absent,         // 404: merge with an empty map, create with If-None-Match: *
  FolderMissing,  // 409 (some servers on a missing parent): MKCOL the folder, then as Absent
  Failed,         // anything else: abort, nothing changed
};
Remote remoteAfterGet(dav::Error error);

// The whole GET-merge-PUT runs at most this often: a 412 (someone uploaded
// between our GET and PUT) restarts it once, a second one gives up.
constexpr uint8_t MAX_ATTEMPTS = 2;

enum class PutNext : uint8_t {
  Done,               // uploaded
  CreateFolderRetry,  // parent collection missing (409 / 404): MKCOL, PUT again
  Restart,            // 412: GET, merge and PUT again
  GiveUpChanged,      // 412 on the last attempt: "changed on server, try again"
  Fail,               // other error: abort (local merge result stays saved)
};

struct PutState {
  uint8_t attempt = 1;         // 1-based GET-merge-PUT round
  bool folderCreated = false;  // MKCOL tried already this sync
};

// Next step after a PUT; updates `state` (attempt / folderCreated).
PutNext afterPut(dav::Error error, PutState& state);

// Counts shown after a sync.
struct Summary {
  unsigned pulled = 0;   // remote entries added or updated locally
  unsigned removed = 0;  // local entries deleted by a remote delete
  bool uploaded = false;
};

// UI text, from tr(): each format takes one %s, the counts ("+3 -1").
struct SummaryText {
  const char* upToDate;         // "Highlights up to date"
  const char* uploadedOnly;     // "Highlights uploaded"
  const char* changes;          // "Highlights: %s"
  const char* changesUploaded;  // "Highlights: %s, uploaded"
};

// One line, ASCII counts ("+3", "-1", "+3 -1"; zero counts left out).
void formatSummary(const Summary& summary, const SummaryText& text, char* out, size_t outSize);

}  // namespace annotationsync
