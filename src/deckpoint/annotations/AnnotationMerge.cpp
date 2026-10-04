#include "AnnotationMerge.h"

#include <algorithm>
#include <string>

#include "AnnotationExport.h"
#include "AnnotationGeometry.h"

namespace deckpoint::annotations {

namespace {

constexpr uint64_t FNV_OFFSET = 1469598103934665603ULL;
constexpr uint64_t FNV_PRIME = 1099511628211ULL;

uint64_t fnvByte(const uint64_t h, const uint8_t b) { return (h ^ b) * FNV_PRIME; }

// Everything the AnnotationSync shape carries, for "did it change" checks.
uint64_t contentHash(const Annotation& a) {
  uint64_t h = FNV_OFFSET;
  for (size_t f = 0; f < FIELD_COUNT; f++) {
    for (const char c : a.get(static_cast<Field>(f))) h = fnvByte(h, static_cast<uint8_t>(c));
    h = fnvByte(h, 0xFF);  // field separator (never a UTF-8 byte)
  }
  h = fnvByte(h, static_cast<uint8_t>(a.deleted | (a.hasPageno << 1) | (a.pageIsNumber << 2)));
  auto n = static_cast<uint32_t>(a.pageno);
  for (int i = 0; i < 4; i++, n >>= 8) h = fnvByte(h, static_cast<uint8_t>(n));
  return h;
}

std::string_view effectiveStamp(const Annotation& a) {
  const std::string_view updated = a.get(Field::DatetimeUpdated);
  return updated.empty() ? a.get(Field::Datetime) : updated;
}

// One side of the merge, with per-entry data computed once.
struct Side {
  explicit Side(AnnotationList& list) : list(list) {}
  AnnotationList& list;
  std::vector<uint64_t> key;            // canonicalKeyHash per list index
  std::vector<std::string_view> stamp;  // comparison stamp per list index
  std::vector<uint8_t> restamp;         // 1: stamp replaced by `now`, written on emit
  std::vector<uint16_t> order;          // merging highlights, position order
};

// annotation_sweep.sort_keys_by_position, made total: pos0, then pos1, then key.
int comparePositions(const Side& s, const uint16_t x, const uint16_t y) {
  const Annotation& a = s.list[x];
  const Annotation& b = s.list[y];
  if (const int c = compareXPointers(a.get(Field::Pos0), b.get(Field::Pos0))) return c;
  if (const int c = compareXPointers(a.get(Field::Pos1), b.get(Field::Pos1))) return c;
  return s.key[x] < s.key[y] ? -1 : s.key[x] > s.key[y] ? 1 : 0;
}

// annotation_sweep.positions_intersect: same key, or either start inside the
// other range, ends inclusive (touching ranges count, as in KOReader).
bool intersects(const Annotation& a, const uint64_t ka, const Annotation& b, const uint64_t kb) {
  if (ka == kb) return true;
  const std::string_view a0 = a.get(Field::Pos0), a1 = a.get(Field::Pos1);
  const std::string_view b0 = b.get(Field::Pos0), b1 = b.get(Field::Pos1);
  if (compareXPointers(a0, b0) <= 0 && compareXPointers(b0, a1) <= 0) return true;
  return compareXPointers(b0, a0) <= 0 && compareXPointers(a0, b1) <= 0;
}

void prepare(Side& s, const bool isLocal, const bool clockOk, const int64_t nowSecs, const std::string_view now,
             MergeReport& report) {
  const size_t n = s.list.size();
  s.key.resize(n);
  s.stamp.resize(n);
  s.restamp.assign(n, 0);
  s.order.reserve(n);
  for (size_t i = 0; i < n; i++) {
    const Annotation& a = s.list[i];
    s.key[i] = canonicalKeyHash(a);
    s.stamp[i] = effectiveStamp(a);
    if (isLocal && a.undated && clockOk) {
      s.stamp[i] = now;  // rule 3
      s.restamp[i] = 1;
    } else if (clockOk) {
      int64_t t;
      if (parseTimestamp(s.stamp[i], t) && t > nowSecs + FUTURE_CLAMP_SECS) {
        // Rule 4. Rewritten to `now` in the result too, so the entry is an
        // ordinary past edit at the next sync instead of winning forever.
        s.stamp[i] = now;
        s.restamp[i] = 1;
        report.clamped++;
      }
    }
    if (a.isHighlight() && !a.localOnly) s.order.push_back(static_cast<uint16_t>(i));
  }
  // Insertion sort: stable, no extra buffer; lists arrive mostly in order.
  for (size_t i = 1; i < s.order.size(); i++) {
    const uint16_t v = s.order[i];
    size_t j = i;
    while (j > 0 && comparePositions(s, v, s.order[j - 1]) < 0) {
      s.order[j] = s.order[j - 1];
      j--;
    }
    s.order[j] = v;
  }
}

// Within the window where neither clock can be trusted to order them.
bool closeInTime(const std::string_view a, const std::string_view b) {
  int64_t ta, tb;
  if (!parseTimestamp(a, ta) || !parseTimestamp(b, tb)) return true;
  return (ta > tb ? ta - tb : tb - ta) <= UNTRUSTED_WINDOW_SECS;
}

struct Emit {
  uint16_t index;
  bool remote;
  bool stamp;             // write `now` into its effective stamp (undated or clamped)
  int16_t combined = -1;  // index into the combined notes: replace the note, stamp updated
};

// Start of the last (possibly partial) UTF-8 character that ends within `max`.
size_t utf8Cut(const std::string_view s, size_t max) {
  if (max >= s.size()) return s.size();
  while (max > 0 && (static_cast<uint8_t>(s[max]) & 0xC0) == 0x80) max--;
  return max;
}

void sortedFingerprints(const AnnotationList& list, const bool includeLocalOnly, std::vector<uint64_t>& out) {
  out.clear();
  out.reserve(list.size());
  for (size_t i = 0; i < list.size(); i++) {
    if (includeLocalOnly || !list[i].localOnly) out.push_back(contentHash(list[i]));
  }
  std::sort(out.begin(), out.end());
}

}  // namespace

bool noteAlreadyMerged(const std::string_view note, const std::string_view other) {
  if (other.empty() || note.find(other) != std::string_view::npos) return true;
  const size_t marker = note.rfind(MERGED_NOTE_MARKER);
  if (marker == std::string_view::npos) return false;
  const std::string_view tail = note.substr(marker + MERGED_NOTE_MARKER.size());
  return !tail.empty() && other.substr(0, tail.size()) == tail;
}

NoteMerge mergeNoteText(std::string& note, const std::string_view other, const size_t maxBytes) {
  if (noteAlreadyMerged(note, other)) return NoteMerge::Unchanged;
  if (note.empty()) {
    note.assign(other.data(), other.size());
    return NoteMerge::Appended;
  }
  const size_t used = note.size() + MERGED_NOTE_MARKER.size();
  const size_t room = used < maxBytes ? maxBytes - used : 0;
  const size_t take = utf8Cut(other, room);
  if (take == 0) return NoteMerge::NoRoom;
  note.append(MERGED_NOTE_MARKER.data(), MERGED_NOTE_MARKER.size());
  note.append(other.data(), take);
  return take < other.size() ? NoteMerge::Truncated : NoteMerge::Appended;
}

void snapshotKeys(const AnnotationList& snapshot, std::vector<uint64_t>& out) {
  out.clear();
  out.reserve(snapshot.size());
  for (size_t i = 0; i < snapshot.size(); i++) out.push_back(canonicalKeyHash(snapshot[i]));
  std::sort(out.begin(), out.end());
}

MergeReport mergeAnnotations(AnnotationList& local, AnnotationList& remote, const std::vector<uint64_t>& snapshot,
                             const std::string_view now, const bool nowCurrent, AnnotationList& merged) {
  MergeReport report;
  if (local.readOnly()) {
    report.status = MergeStatus::LocalReadOnly;
    return report;
  }
  if (remote.readOnly()) {
    report.status = MergeStatus::RemoteUnreadable;
    return report;
  }
  if (!merged.empty() || merged.readOnly()) {
    report.status = MergeStatus::OverBudget;
    return report;
  }

  int64_t nowSecs = 0;
  const bool clockOk = nowCurrent && parseTimestamp(now, nowSecs);
  Side L(local);
  Side R(remote);
  prepare(L, true, clockOk, nowSecs, now, report);
  prepare(R, false, clockOk, nowSecs, now, report);
  const auto uploaded = [&snapshot](const uint64_t key) {
    return std::binary_search(snapshot.begin(), snapshot.end(), key);
  };

  std::vector<Emit> emits;
  emits.reserve(local.size() + remote.size());
  // Rare (overlaps with notes on both devices), so no reserve: usually empty.
  std::vector<std::string> combinedNotes;
  const auto emitLocal = [&](const uint16_t i) { emits.push_back({i, false, L.restamp[i] != 0}); };
  const auto emitRemote = [&](const uint16_t i) { emits.push_back({i, true, R.restamp[i] != 0}); };

  // annotation_sweep.merge: both sides in position order, in step.
  size_t li = 0;
  size_t ri = 0;
  while (li < L.order.size() && ri < R.order.size()) {
    const uint16_t l = L.order[li];
    const uint16_t r = R.order[ri];
    const Annotation& la = local[l];
    const Annotation& ra = remote[r];
    if (!intersects(ra, R.key[r], la, L.key[l])) {
      if (compareXPointers(la.get(Field::Pos0), ra.get(Field::Pos0)) < 0) {
        if (la.deleted && !uploaded(L.key[l])) {
          report.tombstonesDropped++;
        } else {
          emitLocal(l);
        }
        li++;
      } else {
        emitRemote(r);
        if (!ra.deleted) report.added++;
        ri++;
      }
      continue;
    }
    // is_before(income, local): a tie keeps local.
    bool localWins = R.stamp[r] <= L.stamp[l];
    if (localWins && la.deleted && !ra.deleted && !uploaded(R.key[r]) && L.key[l] != R.key[r]) {
      localWins = false;  // rule 5: we never had this one to delete
      report.tombstonesIgnored++;
    }
    if (!la.deleted && !ra.deleted) {
      const Annotation& winner = localWins ? la : ra;
      const Annotation& loser = localWins ? ra : la;
      if (!winner.has(Field::Text) && loser.has(Field::Text) && closeInTime(L.stamp[l], R.stamp[r])) {
        localWins = !localWins;  // rule 6
        report.contentKept++;
      }
    }
    const bool same = contentHash(la) == contentHash(ra) && !L.restamp[l] && !R.restamp[r];
    if (!same) report.conflicts++;
    bool keepLoser = false;
    int16_t combined = -1;
    // A tombstone never takes a different, noted live highlight down with it:
    // the delete propagates and the other highlight stays.
    if (L.key[l] != R.key[r] && (localWins ? la.deleted && !ra.deleted && ra.has(Field::Note)
                                           : ra.deleted && !la.deleted && la.has(Field::Note))) {
      keepLoser = true;
      report.notesRescued++;
    }
    // Two different live highlights collapse: the loser's note goes on.
    if (L.key[l] != R.key[r] && !la.deleted && !ra.deleted) {
      const Annotation& winner = localWins ? la : ra;
      const Annotation& loser = localWins ? ra : la;
      const std::string_view loserNote = loser.get(Field::Note);
      if (!loserNote.empty() && !noteAlreadyMerged(winner.get(Field::Note), loserNote)) {
        std::string note(winner.get(Field::Note));
        // Only a long winner note can leave no room, so the cap is its own.
        const NoteMerge m = mergeNoteText(note, loserNote, std::max(MAX_NOTE_BYTES, note.size()));
        if (m == NoteMerge::NoRoom) {
          keepLoser = true;
          report.notesKeptApart++;
        } else {
          combined = static_cast<int16_t>(combinedNotes.size());
          combinedNotes.push_back(std::move(note));
          report.notesMerged++;
          if (m == NoteMerge::Truncated) report.notesTruncated++;
        }
      }
    }
    if (localWins) {
      emitLocal(l);
      emits.back().combined = combined;
      if (keepLoser) emitRemote(r);
      if (!same && !ra.deleted && !keepLoser) {
        if (la.deleted) report.deletedRemotely++;
        if (la.deleted || (ra.has(Field::Text) && !la.has(Field::Text)) ||
            (ra.has(Field::Note) && !la.has(Field::Note))) {
          report.removesOrBlanks = true;
        }
      }
    } else {
      emitRemote(r);
      emits.back().combined = combined;
      if (keepLoser) emitLocal(l);
      if (!same && !keepLoser) {
        if (ra.deleted && !la.deleted) {
          report.deletedLocally++;
        } else if (!ra.deleted) {
          report.updated++;
        }
      }
    }
    li++;
    ri++;
  }
  for (; li < L.order.size(); li++) {
    const uint16_t l = L.order[li];
    if (local[l].deleted && !uploaded(L.key[l])) {
      report.tombstonesDropped++;  // deleted before it was ever uploaded
    } else {
      emitLocal(l);
    }
  }
  for (; ri < R.order.size(); ri++) {
    emitRemote(R.order[ri]);
    if (!remote[R.order[ri]].deleted) report.added++;
  }

  // Bookmarks: the remote's as they are; ours only for pages it does not have.
  for (size_t i = 0; i < remote.size(); i++) {
    if (!remote[i].isHighlight()) emitRemote(static_cast<uint16_t>(i));
  }
  for (size_t i = 0; i < local.size(); i++) {
    const Annotation& a = local[i];
    if (a.isHighlight() || a.localOnly) continue;
    bool onRemote = false;
    for (size_t j = 0; j < remote.size() && !onRemote; j++) onRemote = !remote[j].isHighlight() && R.key[j] == L.key[i];
    if (!onRemote) emitLocal(static_cast<uint16_t>(i));
  }
  // Seeds stay local.
  for (size_t i = 0; i < local.size(); i++) {
    if (local[i].localOnly) {
      emitLocal(static_cast<uint16_t>(i));
      report.localOnly++;
    }
  }

  // Check the caps before consuming anything.
  size_t bytes = 0;
  for (const Emit& e : emits) {
    const Annotation& a = e.remote ? remote[e.index] : local[e.index];
    bytes += a.heapBytes();
    if (e.stamp) bytes += now.size() > effectiveStamp(a).size() ? now.size() - effectiveStamp(a).size() : 0;
    if (e.combined >= 0) {
      bytes += combinedNotes[e.combined].size() - a.get(Field::Note).size() + now.size();
    }
  }
  if (emits.size() > AnnotationList::MAX_ANNOTATIONS || bytes > merged.blobBudgetBytes()) {
    report = MergeReport{};
    report.status = MergeStatus::OverBudget;
    return report;
  }

  std::vector<uint64_t> localFp;
  std::vector<uint64_t> remoteFp;
  sortedFingerprints(local, true, localFp);
  sortedFingerprints(remote, true, remoteFp);

  merged.reserve(emits.size());
  report.stamped = 0;
  for (const Emit& e : emits) {
    Annotation a = std::move(e.remote ? remote[e.index] : local[e.index]);
    if (e.stamp) {
      const Field f = a.has(Field::DatetimeUpdated) ? Field::DatetimeUpdated : Field::Datetime;
      if (!a.set(f, now)) {
        report.status = MergeStatus::OutOfMemory;
        return report;
      }
      if (a.undated) report.stamped++;
      a.undated = false;
    }
    if (e.combined >= 0) {
      if (!a.set(Field::Note, combinedNotes[e.combined])) {
        report.status = MergeStatus::OutOfMemory;
        return report;
      }
      // The combined note is a new edit. Without a current clock the next
      // sync stamps it (rule 3).
      if (clockOk) {
        if (!a.set(Field::DatetimeUpdated, now)) {
          report.status = MergeStatus::OutOfMemory;
          return report;
        }
      } else {
        a.undated = true;
      }
    }
    // A seed never displaces a real entry with the same key.
    if (a.localOnly && merged.find(annotationKey(a)) >= 0) {
      report.localOnly--;
      continue;
    }
    const AddResult added = merged.adopt(std::move(a));
    if (added != AddResult::Added && added != AddResult::Replaced) {
      report.status = MergeStatus::OutOfMemory;
      return report;
    }
  }
  local.clear();
  remote.clear();

  std::vector<uint64_t> fp;
  sortedFingerprints(merged, false, fp);
  report.remoteChanged = fp != remoteFp;
  sortedFingerprints(merged, true, fp);
  report.localChanged = fp != localFp;
  return report;
}

}  // namespace deckpoint::annotations
