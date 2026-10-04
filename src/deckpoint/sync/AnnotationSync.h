#pragma once

// DECKPOINT: one book's highlight sync with an AnnotationSync.koplugin folder
// over WebDAV, run inside the `:sync` Wi-Fi session (KOReaderSyncActivity):
//   GET <folder>/<md5>.json to SD (404 = empty; its Date header sets the clock)
//   -> abort unless the clock is current (no merging with stale stamps)
//   -> back up the remote before our first upload / a removing merge (rule 7)
//   -> AnnotationStore::mergeRemote (saves the local result)
//   -> PUT with If-Match: <GET ETag> (If-None-Match: * when absent); a
//      missing folder is created once; a 412 reruns GET-merge-PUT once
//   -> save the sync snapshot.
// Failures leave local data as the merge saved it and never delete remote data.

#include <cstddef>
#include <cstdint>
#include <string>

#include "AnnotationSyncPlan.h"
#include "WebDavProtocol.h"

namespace annotationsync {

enum class Outcome : uint8_t {
  Synced,
  NoBook,            // book cannot be hashed / store OOM
  ClockNotSet,       // no SNTP or Date header made the clock current
  ChangedOnServer,   // 412 twice
  Server,            // WebDAV error (davError)
  LocalUnreadable,   // local file or snapshot loaded incompletely
  RemoteUnreadable,  // remote JSON malformed or over limits
  NoMemory,
  SdIo,
};

struct BookResult {
  Outcome outcome = Outcome::Synced;
  dav::Error davError = dav::Error::None;
  Summary summary;
  bool ok() const { return outcome == Outcome::Synced; }
};

// Annotation sync switched on and fully configured.
bool ready();

// Blocking; Wi-Fi must be up. Allocates the WebDAV client (and its TLS
// session) for the call only.
BookResult syncBook(const std::string& bookPath);

// UI text (tr()): line1 is the summary or "Highlight sync failed", line2 the
// reason ("" on success).
void describe(const BookResult& result, char* line1, size_t line1Size, char* line2, size_t line2Size);

}  // namespace annotationsync
