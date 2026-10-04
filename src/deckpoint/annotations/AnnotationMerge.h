#pragma once

// DECKPOINT: AnnotationSync-compatible merge of a book's local annotations with
// the remote map (host-tested, no I/O). Follows annotation_sweep.lua's merge:
// both maps sorted by position, swept in step; an overlapping pair keeps the
// newer one (datetime_updated, else datetime; a tie keeps local), the rest
// passes through. Layered on top (plan "Sync paradigm: surviving time travel"):
//  - undated local entries are first stamped with the sync's `now` (rule 3);
//  - with a current clock, stamps more than FUTURE_CLAMP_SECS ahead of `now`
//    compare as `now` and are rewritten to it in the result (rule 4);
//  - our tombstones (explicit user deletes, kept in the local list) only beat a
//    remote entry we had uploaded before (its key is in the snapshot) or one
//    with the very same position; a remote entry missing locally is new, never
//    "deleted by us" (rule 5; AnnotationSync's find_deleted is not used);
//  - between two live entries within UNTRUSTED_WINDOW_SECS, text never loses
//    to empty text (rule 6);
//  - bookmarks (BOOKMARK|<page>) pass through: remote ones as they are, local
//    ones only where the remote has no entry for that page;
//  - local-only entries (dev seeds) skip the merge and stay local.

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "AnnotationList.h"

namespace deckpoint::annotations {

constexpr int64_t FUTURE_CLAMP_SECS = 86400;
constexpr int64_t UNTRUSTED_WINDOW_SECS = 86400;

enum class MergeStatus : uint8_t {
  Ok,
  LocalReadOnly,       // local list loaded incompletely: refuse (it would be lost)
  RemoteUnreadable,    // remote list lossy / malformed: refuse (upload would drop entries)
  SnapshotUnreadable,  // the last-upload snapshot exists but cannot be read (AnnotationStore)
  OverBudget,          // the result would not fit merged's caps; inputs untouched
  OutOfMemory,         // OOM while building the result; inputs partly consumed, reload them
};

struct MergeReport {
  MergeStatus status = MergeStatus::Ok;
  uint16_t added = 0;              // live remote entries new to local
  uint16_t updated = 0;            // local entries replaced by a newer remote version
  uint16_t deletedLocally = 0;     // remote tombstones that removed a live local entry
  uint16_t deletedRemotely = 0;    // our tombstones that removed a live remote entry
  uint16_t conflicts = 0;          // overlapping pairs whose content differed
  uint16_t stamped = 0;            // undated local entries stamped with `now`
  uint16_t clamped = 0;            // future stamps compared as `now`
  uint16_t contentKept = 0;        // rule 6 overrides
  uint16_t tombstonesIgnored = 0;  // rule 5: our tombstone vs a remote entry never uploaded by us
  uint16_t tombstonesDropped = 0;  // never-uploaded tombstones with nothing to delete
  uint16_t localOnly = 0;          // seeds kept out of the upload
  // Upload differs from what the remote holds: PUT it.
  bool remoteChanged = false;
  // Result differs from the local list: save it.
  bool localChanged = false;
  // The upload deletes or blanks (text / note emptied) a live remote entry:
  // back the remote file up first (rule 7).
  bool removesOrBlanks = false;
};

// Sorted canonical key hashes (canonicalKeyHash) of a map: the snapshot of
// what was last uploaded, as mergeAnnotations takes it.
void snapshotKeys(const AnnotationList& snapshot, std::vector<uint64_t>& out);

// Merges `remote` into `local`, building `merged` (must be empty; its caps
// bound the result). On Ok, `local` and `remote` are consumed (cleared) and
// `merged` is the new local list: entries keep their flags, stamped ones lose
// `undated`. The upload is writeAnnotationMap(merged, sink, false); after a
// successful PUT the same bytes are the new snapshot.
//   snapshot:   snapshotKeys() of the last uploaded map (empty: never uploaded)
//   now:        "YYYY-MM-DD HH:MM:SS" (AnnotationStore::now) at sync time
//   nowCurrent: trustedtime::isCurrent(); without it nothing is stamped or clamped
// On refusal nothing is touched.
MergeReport mergeAnnotations(AnnotationList& local, AnnotationList& remote, const std::vector<uint64_t>& snapshot,
                             std::string_view now, bool nowCurrent, AnnotationList& merged);

}  // namespace deckpoint::annotations
