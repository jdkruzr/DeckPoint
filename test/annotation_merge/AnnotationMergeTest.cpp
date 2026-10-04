// DECKPOINT: AnnotationSync-compatible merge (annotation_sweep.lua semantics plus
// the "surviving time travel" rules) and the local flags sidecar.

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "deckpoint/annotations/Annotation.h"
#include "deckpoint/annotations/AnnotationExport.h"
#include "deckpoint/annotations/AnnotationGeometry.h"
#include "deckpoint/annotations/AnnotationJson.h"
#include "deckpoint/annotations/AnnotationList.h"
#include "deckpoint/annotations/AnnotationMerge.h"

using namespace deckpoint::annotations;

namespace {

constexpr const char* NOW = "2026-10-05 12:00:00";
constexpr const char* OLD = "2026-10-04 10:00:00";
constexpr const char* NEWER = "2026-10-04 11:00:00";

// Legacy spelling: "/body/DocFragment[10]/body/div/p[N]/text().O".
std::string xp(const int para, const int offset, const int fragment = 10) {
  return "/body/DocFragment[" + std::to_string(fragment) + "]/body/div/p[" + std::to_string(para) + "]/text()." +
         std::to_string(offset);
}

struct Spec {
  Spec(std::string pos0 = "", std::string pos1 = "", std::string text = "some words", std::string note = "",
       std::string datetime = OLD, std::string updated = "", const bool deleted = false, const bool undated = false)
      : pos0(std::move(pos0)),
        pos1(std::move(pos1)),
        text(std::move(text)),
        note(std::move(note)),
        datetime(std::move(datetime)),
        updated(std::move(updated)),
        deleted(deleted),
        undated(undated) {}
  std::string pos0;
  std::string pos1;
  std::string text;
  std::string note;
  std::string datetime;
  std::string updated;
  bool deleted;
  bool undated;
  bool localOnly = false;
  std::string bookmarkPage;  // set: a bookmark entry instead of a highlight
};

Annotation make(const Spec& s) {
  Annotation a;
  std::string_view v[FIELD_COUNT];
  if (s.bookmarkPage.empty()) {
    v[static_cast<size_t>(Field::Pos0)] = s.pos0;
    v[static_cast<size_t>(Field::Pos1)] = s.pos1;
    v[static_cast<size_t>(Field::Page)] = s.pos0;
  } else {
    v[static_cast<size_t>(Field::Page)] = s.bookmarkPage;
  }
  v[static_cast<size_t>(Field::Text)] = s.text;
  v[static_cast<size_t>(Field::Note)] = s.note;
  v[static_cast<size_t>(Field::Chapter)] = "Prologue";
  v[static_cast<size_t>(Field::Drawer)] = "lighten";
  v[static_cast<size_t>(Field::Color)] = "gray";
  v[static_cast<size_t>(Field::Datetime)] = s.datetime;
  v[static_cast<size_t>(Field::DatetimeUpdated)] = s.updated;
  EXPECT_TRUE(a.assign(v));
  a.deleted = s.deleted;
  a.undated = s.undated;
  a.localOnly = s.localOnly;
  return a;
}

void put(AnnotationList& list, const Spec& s) {
  const AddResult r = list.adopt(make(s));
  ASSERT_TRUE(r == AddResult::Added || r == AddResult::Replaced);
}

std::string keyOf(const Spec& s) {
  return s.bookmarkPage.empty() ? s.pos0 + "||" + s.pos1 : "BOOKMARK|" + s.bookmarkPage;
}

JsonSink stringSink(std::string& out) {
  return JsonSink{&out, [](void* ctx, const char* data, size_t len) {
                    static_cast<std::string*>(ctx)->append(data, len);
                    return true;
                  }};
}

std::string writeMap(const AnnotationList& list, const bool includeLocalOnly = true) {
  std::string out;
  EXPECT_TRUE(writeAnnotationMap(list, stringSink(out), includeLocalOnly));
  return out;
}

LoadReport loadMap(AnnotationList& list, const std::string& json) {
  AnnotationJsonReader reader(list);
  EXPECT_TRUE(reader.begin());
  for (size_t i = 0; i < json.size(); i += 300) reader.feed(json.data() + i, std::min<size_t>(300, json.size() - i));
  return reader.finish();
}

std::string readFixture(const char* name) {
  std::ifstream in(std::string(DECKPOINT_ANNOTATION_FIXTURES) + "/" + name, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// A full copy of a local list: main file + flags sidecar round trip.
void copyLocal(const AnnotationList& from, AnnotationList& to) {
  ASSERT_FALSE(loadMap(to, writeMap(from)).lossy);
  std::string flags;
  ASSERT_TRUE(writeLocalFlags(from, stringSink(flags)));
  LocalFlagsReader reader(to);
  ASSERT_TRUE(reader.begin());
  reader.feed(flags.data(), flags.size());
  ASSERT_TRUE(reader.finish());
}

struct MergeRun {
  AnnotationList merged;
  MergeReport report;
  std::string upload;
};

// Merges (local, remote) with the snapshot `uploadedBefore` (a map, possibly empty).
void merge(AnnotationList& local, AnnotationList& remote, const AnnotationList& uploadedBefore, MergeRun& run,
           const bool current = true, const char* now = NOW) {
  std::vector<uint64_t> keys;
  snapshotKeys(uploadedBefore, keys);
  run.report = mergeAnnotations(local, remote, keys, now, current, run.merged);
  run.upload = writeMap(run.merged, false);
}

const Annotation* find(const AnnotationList& list, const std::string& key) {
  const int i = list.find(key);
  return i < 0 ? nullptr : &list[static_cast<size_t>(i)];
}

size_t liveCount(const AnnotationList& list) {
  size_t n = 0;
  for (size_t i = 0; i < list.size(); i++) n += list[i].deleted ? 0 : 1;
  return n;
}

}  // namespace

// --- helpers ---------------------------------------------------------------

TEST(AnnotationTimestamp, ParsesKOReaderFormat) {
  int64_t a = 0, b = 0;
  ASSERT_TRUE(parseTimestamp("1970-01-01 00:00:00", a));
  EXPECT_EQ(a, 0);
  ASSERT_TRUE(parseTimestamp("2026-10-04 02:50:02", a));
  ASSERT_TRUE(parseTimestamp("2026-10-05 02:50:02", b));
  EXPECT_EQ(b - a, 86400);
  ASSERT_TRUE(parseTimestamp("2024-03-01 00:00:00", a));
  ASSERT_TRUE(parseTimestamp("2024-02-28 00:00:00", b));
  EXPECT_EQ(a - b, 2 * 86400);  // leap year
  EXPECT_FALSE(parseTimestamp("", a));
  EXPECT_FALSE(parseTimestamp("2026-10-04T02:50:02", a));
  EXPECT_FALSE(parseTimestamp("2026-13-04 02:50:02", a));
  EXPECT_FALSE(parseTimestamp("1969-12-31 23:59:59", a));
  EXPECT_FALSE(parseTimestamp("2026-10-04 02:50:0", a));
}

TEST(AnnotationKeys, SpellingVariantsHashAlike) {
  const Annotation legacy =
      make({"/body/DocFragment[10]/body/div/p/text().0", "/body/DocFragment[10]/body/div/p/text().28"});
  const Annotation explicitForm = make({"/body[1]/DocFragment[10]/body[1]/div[1]/p[1]/text()[1].0",
                                        "/body[1]/DocFragment[10]/body[1]/div[1]/p[1]/text()[1].28"});
  const Annotation compat = make(
      {"/body/DocFragment[10]/body/div[1]/p[1]/text()[1].0", "/body/DocFragment[10]/body/div[1]/p[1]/text()[1].28"});
  EXPECT_EQ(canonicalKeyHash(legacy), canonicalKeyHash(explicitForm));
  EXPECT_EQ(canonicalKeyHash(legacy), canonicalKeyHash(compat));
  EXPECT_NE(canonicalKeyHash(legacy), canonicalKeyHash(make({xp(1, 0), xp(1, 29)})));
  EXPECT_NE(canonicalKeyHash(legacy), canonicalKeyHash(make({xp(2, 0), xp(2, 28)})));
  Spec bookmark;
  bookmark.bookmarkPage = xp(1, 0);
  EXPECT_NE(canonicalKeyHash(make({xp(1, 0), xp(1, 28)})), canonicalKeyHash(make(bookmark)));
}

// --- annotation_sweep.merge semantics ------------------------------------------

TEST(AnnotationMerge, DisjointEntriesFlowBothWays) {
  AnnotationList local, remote, snapshot;
  const Spec ours{xp(1, 0), xp(1, 28), "ours"};
  const Spec theirs{xp(5, 0), xp(5, 20), "theirs"};
  put(local, ours);
  put(remote, theirs);
  MergeRun run;
  merge(local, remote, snapshot, run);
  ASSERT_EQ(run.report.status, MergeStatus::Ok);
  EXPECT_EQ(run.merged.size(), 2u);
  EXPECT_NE(find(run.merged, keyOf(ours)), nullptr);
  EXPECT_NE(find(run.merged, keyOf(theirs)), nullptr);
  EXPECT_EQ(run.report.added, 1);
  EXPECT_EQ(run.report.conflicts, 0);
  EXPECT_TRUE(run.report.remoteChanged);
  EXPECT_TRUE(run.report.localChanged);
  EXPECT_FALSE(run.report.removesOrBlanks);
  EXPECT_TRUE(local.empty());
  EXPECT_TRUE(remote.empty());
}

TEST(AnnotationMerge, OverlapNewerRemoteWins) {
  AnnotationList local, remote, snapshot;
  put(local, {xp(1, 0), xp(1, 28), "words"});
  put(remote, {xp(1, 0), xp(1, 28), "words", "a note from the Boox", OLD, NEWER});
  put(snapshot, {xp(1, 0), xp(1, 28), "words"});
  MergeRun run;
  merge(local, remote, snapshot, run);
  ASSERT_EQ(run.merged.size(), 1u);
  EXPECT_EQ(run.merged[0].get(Field::Note), "a note from the Boox");
  EXPECT_EQ(run.report.updated, 1);
  EXPECT_EQ(run.report.conflicts, 1);
  EXPECT_FALSE(run.report.remoteChanged);
  EXPECT_TRUE(run.report.localChanged);
}

TEST(AnnotationMerge, OverlapNewerLocalWins) {
  AnnotationList local, remote, snapshot;
  put(local, {xp(1, 0), xp(1, 28), "words", "edited here", OLD, NEWER});
  put(remote, {xp(1, 0), xp(1, 28), "words", "older remote note", OLD, "2026-10-04 10:30:00"});
  MergeRun run;
  merge(local, remote, snapshot, run);
  ASSERT_EQ(run.merged.size(), 1u);
  EXPECT_EQ(run.merged[0].get(Field::Note), "edited here");
  EXPECT_TRUE(run.report.remoteChanged);
  EXPECT_FALSE(run.report.localChanged);
}

TEST(AnnotationMerge, EqualTimestampsKeepLocal) {
  AnnotationList local, remote, snapshot;
  put(local, {xp(1, 0), xp(1, 28), "words", "local"});
  put(remote, {xp(1, 0), xp(1, 28), "words", "remote"});
  MergeRun run;
  merge(local, remote, snapshot, run);
  ASSERT_EQ(run.merged.size(), 1u);
  EXPECT_EQ(run.merged[0].get(Field::Note), "local");
}

TEST(AnnotationMerge, PartialOverlapKeepsOnlyTheNewer) {
  AnnotationList local, remote, snapshot;
  const Spec ours{xp(1, 0), xp(1, 28), "ours"};
  const Spec theirs{xp(1, 10), xp(1, 40), "theirs", "", NEWER};
  put(local, ours);
  put(remote, theirs);
  MergeRun run;
  merge(local, remote, snapshot, run);
  ASSERT_EQ(run.merged.size(), 1u);
  EXPECT_NE(find(run.merged, keyOf(theirs)), nullptr);
}

TEST(AnnotationMerge, TouchingRangesOverlapLikeKOReader) {
  // positions_intersect compares ends inclusively.
  AnnotationList local, remote, snapshot;
  put(local, {xp(1, 0), xp(1, 10), "ours", "", NEWER});
  put(remote, {xp(1, 10), xp(1, 20), "theirs"});
  MergeRun run;
  merge(local, remote, snapshot, run);
  EXPECT_EQ(run.merged.size(), 1u);
}

TEST(AnnotationMerge, SpellingVariantsAreTheSamePosition) {
  AnnotationList local, remote, snapshot;
  const Spec legacy{"/body/DocFragment[10]/body/div/p/text().0", "/body/DocFragment[10]/body/div/p/text().28"};
  const Spec explicitForm{"/body[1]/DocFragment[10]/body[1]/div[1]/p[1]/text()[1].0",
                          "/body[1]/DocFragment[10]/body[1]/div[1]/p[1]/text()[1].28",
                          "some words",
                          "note",
                          OLD,
                          NEWER};
  put(local, legacy);
  put(remote, explicitForm);
  put(snapshot, legacy);
  MergeRun run;
  merge(local, remote, snapshot, run);
  ASSERT_EQ(run.merged.size(), 1u);
  EXPECT_NE(find(run.merged, keyOf(explicitForm)), nullptr);
  EXPECT_EQ(run.report.added, 0);
  EXPECT_EQ(run.report.updated, 1);
}

TEST(AnnotationMerge, ChaptersOrderNumerically) {
  // DocFragment[9] < DocFragment[10]: the sweep must not pair across chapters.
  AnnotationList local, remote, snapshot;
  put(local, {xp(1, 0, 9), xp(1, 5, 9), "nine"});
  put(local, {xp(1, 0, 10), xp(1, 5, 10), "ten"});
  put(remote, {xp(1, 0, 10), xp(1, 5, 10), "ten"});
  put(remote, {xp(1, 0, 171), xp(1, 5, 171), "far"});
  MergeRun run;
  merge(local, remote, snapshot, run);
  EXPECT_EQ(run.merged.size(), 3u);
  EXPECT_EQ(run.report.conflicts, 0);
  EXPECT_EQ(run.report.added, 1);
}

// --- tombstones (rule 5) --------------------------------------------------------

TEST(AnnotationMerge, OurTombstoneDeletesAnUploadedRemoteEntry) {
  AnnotationList local, remote, snapshot;
  const Spec live{xp(1, 0), xp(1, 28)};
  Spec gone = live;
  gone.deleted = true;
  gone.updated = NEWER;
  put(local, gone);
  put(remote, live);
  put(snapshot, live);
  MergeRun run;
  merge(local, remote, snapshot, run);
  ASSERT_EQ(run.merged.size(), 1u);
  EXPECT_TRUE(run.merged[0].deleted);
  EXPECT_EQ(run.report.deletedRemotely, 1);
  EXPECT_TRUE(run.report.remoteChanged);
  EXPECT_TRUE(run.report.removesOrBlanks);
  EXPECT_NE(run.upload.find("\"deleted\":true"), std::string::npos);
}

TEST(AnnotationMerge, RemoteTombstoneRemovesLocalEntry) {
  AnnotationList local, remote, snapshot;
  const Spec live{xp(1, 0), xp(1, 28)};
  Spec gone = live;
  gone.deleted = true;
  gone.updated = NEWER;
  put(local, live);
  put(remote, gone);
  put(snapshot, live);
  MergeRun run;
  merge(local, remote, snapshot, run);
  ASSERT_EQ(run.merged.size(), 1u);
  EXPECT_TRUE(run.merged[0].deleted);
  EXPECT_EQ(run.report.deletedLocally, 1);
  EXPECT_FALSE(run.report.remoteChanged);
  EXPECT_EQ(liveCount(run.merged), 0u);
}

TEST(AnnotationMerge, RemoteEntryMissingLocallyIsNewNotDeleted) {
  // In the snapshot, gone locally without a tombstone (e.g. a wiped local
  // file): AnnotationSync's find_deleted would delete it; we take it back.
  AnnotationList local, remote, snapshot;
  const Spec entry{xp(3, 0), xp(3, 12)};
  put(remote, entry);
  put(snapshot, entry);
  put(local, {xp(9, 0), xp(9, 5), "other"});
  MergeRun run;
  merge(local, remote, snapshot, run);
  EXPECT_EQ(run.merged.size(), 2u);
  ASSERT_NE(find(run.merged, keyOf(entry)), nullptr);
  EXPECT_FALSE(find(run.merged, keyOf(entry))->deleted);
  EXPECT_EQ(run.report.added, 1);
  EXPECT_EQ(run.report.deletedRemotely, 0);
}

TEST(AnnotationMerge, LocalEntryNeverUploadedIsKept) {
  AnnotationList local, remote, snapshot;
  const Spec entry{xp(3, 0), xp(3, 12)};
  put(local, entry);
  put(remote, {xp(7, 0), xp(7, 3)});
  MergeRun run;
  merge(local, remote, snapshot, run);
  EXPECT_NE(find(run.merged, keyOf(entry)), nullptr);
  EXPECT_NE(run.upload.find("p[3]"), std::string::npos);
}

TEST(AnnotationMerge, LocalEntryMissingFromRemoteIsUploadedAgain) {
  AnnotationList local, remote, snapshot;
  const Spec entry{xp(3, 0), xp(3, 12)};
  put(local, entry);
  put(snapshot, entry);
  MergeRun run;
  merge(local, remote, snapshot, run);
  EXPECT_EQ(run.merged.size(), 1u);
  EXPECT_TRUE(run.report.remoteChanged);
}

TEST(AnnotationMerge, TombstoneNeverBeatsAnEntryWeNeverHad) {
  // We deleted X; another device made an overlapping Y (older stamp) that we
  // never downloaded. Y is not ours to delete.
  AnnotationList local, remote, snapshot;
  const Spec x{xp(1, 0), xp(1, 28)};
  Spec xGone = x;
  xGone.deleted = true;
  xGone.updated = NEWER;
  const Spec y{xp(1, 5), xp(1, 40), "their words"};
  put(local, xGone);
  put(remote, y);
  put(snapshot, x);
  MergeRun run;
  merge(local, remote, snapshot, run);
  ASSERT_EQ(run.merged.size(), 1u);
  EXPECT_FALSE(run.merged[0].deleted);
  EXPECT_NE(find(run.merged, keyOf(y)), nullptr);
  EXPECT_EQ(run.report.tombstonesIgnored, 1);
  EXPECT_EQ(run.report.deletedRemotely, 0);
}

TEST(AnnotationMerge, TombstoneForTheSamePositionNeedsNoSnapshot) {
  AnnotationList local, remote, snapshot;
  const Spec x{xp(1, 0), xp(1, 28)};
  Spec xGone = x;
  xGone.deleted = true;
  xGone.updated = NEWER;
  put(local, xGone);
  put(remote, x);
  MergeRun run;
  merge(local, remote, snapshot, run);
  ASSERT_EQ(run.merged.size(), 1u);
  EXPECT_TRUE(run.merged[0].deleted);
}

TEST(AnnotationMerge, NeverUploadedTombstoneIsDropped) {
  AnnotationList local, remote, snapshot;
  put(local, {xp(1, 0), xp(1, 28), "x", "", OLD, NEWER, true});
  MergeRun run;
  merge(local, remote, snapshot, run);
  EXPECT_TRUE(run.merged.empty());
  EXPECT_EQ(run.report.tombstonesDropped, 1);
  EXPECT_FALSE(run.report.remoteChanged);
  EXPECT_TRUE(run.report.localChanged);
}

TEST(AnnotationMerge, UploadedTombstoneStaysWhenRemoteLostIt) {
  AnnotationList local, remote, snapshot;
  const Spec x{xp(1, 0), xp(1, 28)};
  Spec xGone = x;
  xGone.deleted = true;
  xGone.updated = NEWER;
  put(local, xGone);
  put(snapshot, x);
  MergeRun run;
  merge(local, remote, snapshot, run);
  ASSERT_EQ(run.merged.size(), 1u);
  EXPECT_TRUE(run.merged[0].deleted);
}

// --- time travel (rules 3, 4, 6) --------------------------------------------------

TEST(AnnotationMerge, UndatedLocalEditIsStampedAndBeatsStaleRemote) {
  // Cold boot: the floor-restored clock is a day stale, so our later edit
  // carries an older stamp than the Boox's earlier edit.
  AnnotationList local, remote, snapshot;
  const Spec base{xp(1, 0), xp(1, 28)};
  Spec ours = base;
  ours.note = "latest, made on the T-Deck";
  ours.updated = "2026-10-03 09:00:00";
  ours.undated = true;
  Spec theirs = base;
  theirs.note = "older edit on the Boox";
  theirs.updated = "2026-10-04 08:00:00";
  put(local, ours);
  put(remote, theirs);
  put(snapshot, base);
  MergeRun run;
  merge(local, remote, snapshot, run);
  ASSERT_EQ(run.merged.size(), 1u);
  EXPECT_EQ(run.merged[0].get(Field::Note), "latest, made on the T-Deck");
  EXPECT_EQ(run.merged[0].get(Field::DatetimeUpdated), NOW);
  EXPECT_FALSE(run.merged[0].undated);
  EXPECT_EQ(run.report.stamped, 1);
  EXPECT_TRUE(run.report.remoteChanged);
}

TEST(AnnotationMerge, UndatedCreationStampsDatetime) {
  AnnotationList local, remote, snapshot;
  put(local, {xp(1, 0), xp(1, 28), "new", "", "1970-01-01 00:00:00", "", false, true});
  MergeRun run;
  merge(local, remote, snapshot, run);
  ASSERT_EQ(run.merged.size(), 1u);
  EXPECT_EQ(run.merged[0].get(Field::Datetime), NOW);
  EXPECT_FALSE(run.merged[0].has(Field::DatetimeUpdated));
  EXPECT_FALSE(run.merged[0].undated);
}

TEST(AnnotationMerge, NothingIsStampedWithoutACurrentClock) {
  AnnotationList local, remote, snapshot;
  put(local, {xp(1, 0), xp(1, 28), "w", "ours", OLD, "2026-10-03 09:00:00", false, true});
  put(remote, {xp(1, 0), xp(1, 28), "w", "theirs", OLD, "2026-10-04 08:00:00"});
  MergeRun run;
  merge(local, remote, snapshot, run, /*current=*/false);
  ASSERT_EQ(run.merged.size(), 1u);
  EXPECT_EQ(run.merged[0].get(Field::Note), "theirs");
  EXPECT_EQ(run.report.stamped, 0);
}

TEST(AnnotationMerge, UndatedLocalOnlyEntryStaysUndatedWithoutCurrentClock) {
  AnnotationList local, remote, snapshot;
  put(local, {xp(1, 0), xp(1, 28), "w", "", "2026-10-03 09:00:00", "", false, true});
  MergeRun run;
  merge(local, remote, snapshot, run, /*current=*/false);
  ASSERT_EQ(run.merged.size(), 1u);
  EXPECT_TRUE(run.merged[0].undated);
  EXPECT_EQ(run.merged[0].get(Field::Datetime), "2026-10-03 09:00:00");
}

TEST(AnnotationMerge, FutureRemoteStampIsClampedToNow) {
  AnnotationList local, remote, snapshot;
  const Spec base{xp(1, 0), xp(1, 28)};
  Spec ours = base;
  ours.note = "ours";
  ours.updated = "2026-10-01 09:00:00";
  ours.undated = true;  // stamped NOW: ties with the clamped remote, local keeps the tie
  Spec broken = base;
  broken.note = "from a device living in 2031";
  broken.updated = "2031-01-01 00:00:00";
  put(local, ours);
  put(remote, broken);
  MergeRun run;
  merge(local, remote, snapshot, run);
  ASSERT_EQ(run.merged.size(), 1u);
  EXPECT_EQ(run.merged[0].get(Field::Note), "ours");
  EXPECT_EQ(run.report.clamped, 1);
}

TEST(AnnotationMerge, ClampedWinnerIsRewrittenToNow) {
  AnnotationList local, remote, snapshot;
  put(local, {xp(1, 0), xp(1, 28), "w", "ours", OLD, NEWER});
  put(remote, {xp(1, 0), xp(1, 28), "w", "future", OLD, "2031-01-01 00:00:00"});
  MergeRun run;
  merge(local, remote, snapshot, run);
  ASSERT_EQ(run.merged.size(), 1u);
  EXPECT_EQ(run.merged[0].get(Field::Note), "future");
  EXPECT_EQ(run.merged[0].get(Field::DatetimeUpdated), NOW);  // no longer wins forever
  EXPECT_TRUE(run.report.remoteChanged);

  // A later local edit now beats it.
  AnnotationList local2, remote2, snapshot2;
  ASSERT_FALSE(loadMap(remote2, run.upload).lossy);
  ASSERT_FALSE(loadMap(snapshot2, run.upload).lossy);
  put(local2, {xp(1, 0), xp(1, 28), "w", "edited after the sync", OLD, "2026-10-05 13:00:00"});
  MergeRun run2;
  merge(local2, remote2, snapshot2, run2, true, "2026-10-05 14:00:00");
  ASSERT_EQ(run2.merged.size(), 1u);
  EXPECT_EQ(run2.merged[0].get(Field::Note), "edited after the sync");
}

TEST(AnnotationMerge, FutureStampsAreNotClampedWithoutACurrentClock) {
  AnnotationList local, remote, snapshot;
  put(remote, {xp(1, 0), xp(1, 28), "w", "", OLD, "2031-01-01 00:00:00"});
  MergeRun run;
  merge(local, remote, snapshot, run, /*current=*/false);
  EXPECT_EQ(run.report.clamped, 0);
  EXPECT_EQ(run.merged[0].get(Field::DatetimeUpdated), "2031-01-01 00:00:00");
}

TEST(AnnotationMerge, ContentBeatsEmptyWithinTheUntrustedWindow) {
  AnnotationList local, remote, snapshot;
  put(local, {xp(1, 0), xp(1, 28), "", "", NEWER});  // a seed-like empty entry, newer
  put(remote, {xp(1, 0), xp(1, 28), "the real words", "", OLD});
  MergeRun run;
  merge(local, remote, snapshot, run);
  ASSERT_EQ(run.merged.size(), 1u);
  EXPECT_EQ(run.merged[0].get(Field::Text), "the real words");
  EXPECT_EQ(run.report.contentKept, 1);
}

TEST(AnnotationMerge, ContentKeptAgainstEmptyRemoteToo) {
  AnnotationList local, remote, snapshot;
  put(local, {xp(1, 0), xp(1, 28), "ours", "", OLD});
  put(remote, {xp(1, 0), xp(1, 28), "", "", NEWER});
  MergeRun run;
  merge(local, remote, snapshot, run);
  ASSERT_EQ(run.merged.size(), 1u);
  EXPECT_EQ(run.merged[0].get(Field::Text), "ours");
  EXPECT_EQ(run.report.contentKept, 1);
}

TEST(AnnotationMerge, EmptyWinsWhenClearlyNewer) {
  AnnotationList local, remote, snapshot;
  put(local, {xp(1, 0), xp(1, 28), "", "", "2026-10-07 10:00:00"});
  put(remote, {xp(1, 0), xp(1, 28), "words", "", OLD});
  MergeRun run;
  merge(local, remote, snapshot, run, true, "2026-10-07 12:00:00");
  ASSERT_EQ(run.merged.size(), 1u);
  EXPECT_EQ(run.merged[0].get(Field::Text), "");
  EXPECT_EQ(run.report.contentKept, 0);
  EXPECT_TRUE(run.report.removesOrBlanks);
}

TEST(AnnotationMerge, NoteRemovalIsAnOrdinaryEdit) {
  AnnotationList local, remote, snapshot;
  put(local, {xp(1, 0), xp(1, 28), "words", "", OLD, NEWER});
  put(remote, {xp(1, 0), xp(1, 28), "words", "old note", OLD});
  MergeRun run;
  merge(local, remote, snapshot, run);
  ASSERT_EQ(run.merged.size(), 1u);
  EXPECT_FALSE(run.merged[0].has(Field::Note));
  EXPECT_TRUE(run.report.removesOrBlanks);  // back the remote up first (rule 7)
}

// --- bookmarks, seeds, refusals ------------------------------------------------

TEST(AnnotationMerge, BookmarksPassThrough) {
  AnnotationList local, remote, snapshot;
  Spec theirs;
  theirs.bookmarkPage = xp(4, 0);
  theirs.text = "remote bookmark";
  Spec oursSamePage = theirs;
  oursSamePage.text = "stale local copy";
  oursSamePage.datetime = NEWER;
  Spec oursOther;
  oursOther.bookmarkPage = xp(8, 0);
  put(remote, theirs);
  put(local, oursSamePage);
  put(local, oursOther);
  MergeRun run;
  merge(local, remote, snapshot, run);
  ASSERT_EQ(run.merged.size(), 2u);
  EXPECT_EQ(find(run.merged, keyOf(theirs))->get(Field::Text), "remote bookmark");
  EXPECT_NE(find(run.merged, keyOf(oursOther)), nullptr);
}

TEST(AnnotationMerge, LocalOnlySeedsAreNeverUploaded) {
  AnnotationList local, remote, snapshot;
  Spec seed{xp(2, 0), xp(2, 9), ""};
  seed.localOnly = true;
  const Spec real{xp(6, 0), xp(6, 9)};
  put(local, seed);
  put(remote, real);
  put(snapshot, real);
  MergeRun run;
  merge(local, remote, snapshot, run);
  ASSERT_EQ(run.merged.size(), 2u);
  ASSERT_NE(find(run.merged, keyOf(seed)), nullptr);
  EXPECT_TRUE(find(run.merged, keyOf(seed))->localOnly);
  EXPECT_EQ(run.upload.find("p[2]"), std::string::npos);
  EXPECT_EQ(run.report.localOnly, 1);
  EXPECT_FALSE(run.report.remoteChanged);
}

TEST(AnnotationMerge, SeedNeverOverlapsOrDisplacesARemoteEntry) {
  AnnotationList local, remote, snapshot;
  Spec seed{xp(2, 0), xp(2, 9), "", "", NEWER};
  seed.localOnly = true;
  const Spec real{xp(2, 0), xp(2, 9), "real"};
  put(local, seed);
  put(remote, real);
  MergeRun run;
  merge(local, remote, snapshot, run);
  ASSERT_EQ(run.merged.size(), 1u);
  EXPECT_EQ(run.merged[0].get(Field::Text), "real");
  EXPECT_FALSE(run.merged[0].localOnly);
  EXPECT_EQ(run.report.localOnly, 0);
}

TEST(AnnotationMerge, ReadOnlyLocalIsRefusedUntouched) {
  AnnotationList local, remote, snapshot;
  put(local, {xp(1, 0), xp(1, 5)});
  put(remote, {xp(2, 0), xp(2, 5)});
  local.markReadOnly();
  MergeRun run;
  merge(local, remote, snapshot, run);
  EXPECT_EQ(run.report.status, MergeStatus::LocalReadOnly);
  EXPECT_EQ(local.size(), 1u);
  EXPECT_EQ(remote.size(), 1u);
  EXPECT_TRUE(run.merged.empty());
}

TEST(AnnotationMerge, LossyRemoteIsRefused) {
  AnnotationList local, remote, snapshot;
  put(local, {xp(1, 0), xp(1, 5)});
  EXPECT_TRUE(loadMap(remote, "{\"a||b\":{\"pos0\":\"a\",").lossy);
  MergeRun run;
  merge(local, remote, snapshot, run);
  EXPECT_EQ(run.report.status, MergeStatus::RemoteUnreadable);
  EXPECT_EQ(local.size(), 1u);
}

TEST(AnnotationMerge, EmptyLocalNeverWipesTheRemote) {
  AnnotationList local, remote, snapshot;
  ASSERT_FALSE(loadMap(remote, readFixture("koreader_remote.json")).lossy);
  ASSERT_FALSE(loadMap(snapshot, readFixture("koreader_remote.json")).lossy);
  const size_t n = remote.size();
  MergeRun run;
  merge(local, remote, snapshot, run);
  ASSERT_EQ(run.report.status, MergeStatus::Ok);
  EXPECT_EQ(run.merged.size(), n);
  EXPECT_EQ(liveCount(run.merged), n);
  EXPECT_FALSE(run.report.remoteChanged);
  EXPECT_FALSE(run.report.removesOrBlanks);
}

TEST(AnnotationMerge, OverBudgetLeavesInputsUntouched) {
  AnnotationList local, remote, snapshot;
  put(local, {xp(1, 0), xp(1, 5), std::string(300, 'a')});
  put(remote, {xp(2, 0), xp(2, 5), std::string(300, 'b')});
  std::vector<uint64_t> keys;
  AnnotationList merged(/*blobBudget=*/400);
  const MergeReport report = mergeAnnotations(local, remote, keys, NOW, true, merged);
  EXPECT_EQ(report.status, MergeStatus::OverBudget);
  EXPECT_EQ(local.size(), 1u);
  EXPECT_EQ(local[0].get(Field::Text).size(), 300u);
  EXPECT_EQ(remote.size(), 1u);
  EXPECT_TRUE(merged.empty());
}

// --- real shape, idempotence -------------------------------------------------------

TEST(AnnotationMerge, RealRemoteShapeRoundTrips) {
  // First sync of a book read on the Boox: our new highlight joins its six.
  AnnotationList local, remote, snapshot;
  const std::string fixture = readFixture("koreader_remote.json");
  ASSERT_FALSE(loadMap(remote, fixture).lossy);
  const Spec ours{xp(2, 0, 12), xp(2, 30, 12), "made on the T-Deck"};
  put(local, ours);
  MergeRun run;
  merge(local, remote, snapshot, run);
  ASSERT_EQ(run.report.status, MergeStatus::Ok);
  AnnotationList uploaded;
  ASSERT_FALSE(loadMap(uploaded, run.upload).lossy);
  AnnotationList original;
  ASSERT_FALSE(loadMap(original, fixture).lossy);
  EXPECT_EQ(uploaded.size(), original.size() + 1);
  for (size_t i = 0; i < original.size(); i++) {
    const Annotation* u = find(uploaded, annotationKey(original[i]));
    ASSERT_NE(u, nullptr);
    for (size_t f = 0; f < FIELD_COUNT; f++) {
      EXPECT_EQ(u->get(static_cast<Field>(f)), original[i].get(static_cast<Field>(f)));
    }
    EXPECT_EQ(u->pageno, original[i].pageno);
  }
  EXPECT_EQ(run.report.added, original.size());
}

TEST(AnnotationMerge, MergeIsIdempotent) {
  AnnotationList local, remote, snapshot;
  ASSERT_FALSE(loadMap(remote, readFixture("koreader_remote.json")).lossy);
  ASSERT_FALSE(loadMap(snapshot, readFixture("koreader_remote.json")).lossy);
  // Ours: a new highlight, an undated note edit on a remote one, a delete,
  // a seed, a never-uploaded tombstone.
  put(local, {xp(2, 0, 12), xp(2, 30, 12), "new here"});
  for (size_t i = 0; i < snapshot.size(); i++) {
    const Annotation& s = snapshot[i];
    if (i == 0) {
      Spec edited{std::string(s.get(Field::Pos0)),
                  std::string(s.get(Field::Pos1)),
                  std::string(s.get(Field::Text)),
                  "an undated note",
                  std::string(s.get(Field::Datetime)),
                  "2026-10-03 00:00:00",
                  false,
                  true};
      put(local, edited);
    } else if (i == 1) {
      Spec gone{std::string(s.get(Field::Pos0)),
                std::string(s.get(Field::Pos1)),
                std::string(s.get(Field::Text)),
                "",
                std::string(s.get(Field::Datetime)),
                NEWER,
                true};
      put(local, gone);
    }
  }
  Spec seed{xp(3, 0, 12), xp(3, 4, 12), ""};
  seed.localOnly = true;
  put(local, seed);
  put(local, {xp(9, 0, 30), xp(9, 4, 30), "x", "", OLD, NEWER, true});

  MergeRun first;
  merge(local, remote, snapshot, first);
  ASSERT_EQ(first.report.status, MergeStatus::Ok);
  EXPECT_TRUE(first.report.remoteChanged);
  EXPECT_EQ(first.report.stamped, 1);
  EXPECT_EQ(first.report.deletedRemotely, 1);
  EXPECT_EQ(first.report.tombstonesDropped, 1);

  // Second sync: the remote now holds our upload, the snapshot is the upload.
  AnnotationList local2, remote2, snapshot2;
  copyLocal(first.merged, local2);
  ASSERT_FALSE(loadMap(remote2, first.upload).lossy);
  ASSERT_FALSE(loadMap(snapshot2, first.upload).lossy);
  MergeRun second;
  merge(local2, remote2, snapshot2, second, true, "2026-10-05 12:30:00");
  ASSERT_EQ(second.report.status, MergeStatus::Ok);
  EXPECT_FALSE(second.report.remoteChanged);
  EXPECT_FALSE(second.report.localChanged);
  EXPECT_EQ(second.report.conflicts, 0);
  EXPECT_EQ(second.report.stamped, 0);
  EXPECT_EQ(second.upload, first.upload);
  EXPECT_EQ(writeMap(second.merged), writeMap(first.merged));
}

// --- notes never die in an overlap ------------------------------------------------

TEST(NoteText, MergeAndContainment) {
  std::string note = "mine";
  EXPECT_EQ(mergeNoteText(note, ""), NoteMerge::Unchanged);
  EXPECT_EQ(mergeNoteText(note, "in"), NoteMerge::Unchanged);  // contained
  EXPECT_EQ(mergeNoteText(note, "theirs"), NoteMerge::Appended);
  EXPECT_EQ(note, std::string("mine") + std::string(MERGED_NOTE_MARKER) + "theirs");
  EXPECT_EQ(mergeNoteText(note, "theirs"), NoteMerge::Unchanged);
  std::string empty;
  EXPECT_EQ(mergeNoteText(empty, "only theirs"), NoteMerge::Appended);
  EXPECT_EQ(empty, "only theirs");
}

TEST(NoteText, TruncatesAtUtf8BoundaryAndStaysIdempotent) {
  std::string note(20, 'a');
  const std::string other = "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9";  // 4 x U+00E9, 8 bytes
  const size_t cap = note.size() + MERGED_NOTE_MARKER.size() + 5;
  ASSERT_EQ(mergeNoteText(note, other, cap), NoteMerge::Truncated);
  EXPECT_LE(note.size(), cap);
  EXPECT_EQ(note.substr(note.size() - 4), "\xC3\xA9\xC3\xA9");  // whole characters only
  EXPECT_TRUE(noteAlreadyMerged(note, other));
  EXPECT_EQ(mergeNoteText(note, other, cap), NoteMerge::Unchanged);
  std::string full(30, 'b');
  EXPECT_EQ(mergeNoteText(full, "x", full.size() + MERGED_NOTE_MARKER.size()), NoteMerge::NoRoom);
  EXPECT_EQ(full, std::string(30, 'b'));
}

TEST(AnnotationMerge, LoserNoteIsAppendedWhenRemoteWins) {
  AnnotationList local, remote, snapshot;
  put(local, {xp(1, 0), xp(1, 28), "ours", "T-Deck thought"});
  const Spec theirs{xp(1, 10), xp(1, 40), "theirs", "Boox thought", NEWER};
  put(remote, theirs);
  MergeRun run;
  merge(local, remote, snapshot, run);
  ASSERT_EQ(run.report.status, MergeStatus::Ok);
  ASSERT_EQ(run.merged.size(), 1u);
  const Annotation* w = find(run.merged, keyOf(theirs));
  ASSERT_NE(w, nullptr);
  EXPECT_EQ(w->get(Field::Note), std::string("Boox thought") + std::string(MERGED_NOTE_MARKER) + "T-Deck thought");
  EXPECT_EQ(w->get(Field::DatetimeUpdated), NOW);
  EXPECT_EQ(run.report.notesMerged, 1);
  EXPECT_TRUE(run.report.remoteChanged);
  EXPECT_TRUE(run.report.localChanged);
}

TEST(AnnotationMerge, LoserNoteIsAppendedWhenLocalWins) {
  AnnotationList local, remote, snapshot;
  const Spec ours{xp(1, 0), xp(1, 28), "ours", "T-Deck thought", NEWER};
  put(local, ours);
  put(remote, {xp(1, 10), xp(1, 40), "theirs", "Boox thought"});
  MergeRun run;
  merge(local, remote, snapshot, run);
  ASSERT_EQ(run.merged.size(), 1u);
  const Annotation* w = find(run.merged, keyOf(ours));
  ASSERT_NE(w, nullptr);
  EXPECT_EQ(w->get(Field::Note), std::string("T-Deck thought") + std::string(MERGED_NOTE_MARKER) + "Boox thought");
  EXPECT_EQ(w->get(Field::DatetimeUpdated), NOW);
  EXPECT_TRUE(run.report.remoteChanged);
}

TEST(AnnotationMerge, OnlyLoserHasNote) {
  AnnotationList local, remote, snapshot;
  put(local, {xp(1, 0), xp(1, 28), "ours", "keep me"});
  const Spec theirs{xp(1, 10), xp(1, 40), "theirs", "", NEWER};
  put(remote, theirs);
  MergeRun run;
  merge(local, remote, snapshot, run);
  ASSERT_EQ(run.merged.size(), 1u);
  EXPECT_EQ(find(run.merged, keyOf(theirs))->get(Field::Note), "keep me");
  EXPECT_EQ(run.report.notesMerged, 1);
}

TEST(AnnotationMerge, OnlyWinnerHasNoteNothingChanges) {
  AnnotationList local, remote, snapshot;
  put(local, {xp(1, 0), xp(1, 28), "ours"});
  const Spec theirs{xp(1, 10), xp(1, 40), "theirs", "winner note", NEWER};
  put(remote, theirs);
  MergeRun run;
  merge(local, remote, snapshot, run);
  ASSERT_EQ(run.merged.size(), 1u);
  EXPECT_EQ(find(run.merged, keyOf(theirs))->get(Field::Note), "winner note");
  EXPECT_FALSE(find(run.merged, keyOf(theirs))->has(Field::DatetimeUpdated));
  EXPECT_EQ(run.report.notesMerged, 0);
  EXPECT_FALSE(run.report.remoteChanged);
}

TEST(AnnotationMerge, SameHighlightNoteEditIsNotMerged) {
  // Same key: the older note is a previous version, not a second thought.
  AnnotationList local, remote, snapshot;
  put(local, {xp(1, 0), xp(1, 28), "words", "first draft"});
  put(remote, {xp(1, 0), xp(1, 28), "words", "rewritten", OLD, NEWER});
  MergeRun run;
  merge(local, remote, snapshot, run);
  ASSERT_EQ(run.merged.size(), 1u);
  EXPECT_EQ(run.merged[0].get(Field::Note), "rewritten");
  EXPECT_EQ(run.report.notesMerged, 0);
}

TEST(AnnotationMerge, MergedNoteIsCutAtTheCap) {
  AnnotationList local, remote, snapshot;
  const std::string longNote(MAX_NOTE_BYTES - 100, 'w');
  const Spec theirs{xp(1, 10), xp(1, 40), "theirs", longNote, NEWER};
  put(remote, theirs);
  put(local, {xp(1, 0), xp(1, 28), "ours", std::string(300, 'l')});
  MergeRun run;
  merge(local, remote, snapshot, run);
  ASSERT_EQ(run.merged.size(), 1u);
  const std::string_view note = find(run.merged, keyOf(theirs))->get(Field::Note);
  EXPECT_EQ(note.size(), MAX_NOTE_BYTES);
  EXPECT_EQ(note.substr(0, longNote.size()), longNote);
  EXPECT_EQ(run.report.notesTruncated, 1);
}

TEST(AnnotationMerge, NoRoomKeepsBothEntries) {
  AnnotationList local, remote, snapshot;
  const Spec theirs{xp(1, 10), xp(1, 40), "theirs", std::string(MAX_NOTE_BYTES, 'w'), NEWER};
  const Spec ours{xp(1, 0), xp(1, 28), "ours", "do not lose me"};
  put(remote, theirs);
  put(local, ours);
  MergeRun run;
  merge(local, remote, snapshot, run);
  ASSERT_EQ(run.report.status, MergeStatus::Ok);
  EXPECT_EQ(run.merged.size(), 2u);
  EXPECT_EQ(find(run.merged, keyOf(ours))->get(Field::Note), "do not lose me");
  EXPECT_EQ(find(run.merged, keyOf(theirs))->get(Field::Note).size(), MAX_NOTE_BYTES);
  EXPECT_EQ(run.report.notesKeptApart, 1);
  EXPECT_EQ(run.report.conflicts, 1);
  EXPECT_TRUE(run.report.remoteChanged);
}

TEST(AnnotationMerge, TombstoneDoesNotTakeANotedNeighbourDown) {
  AnnotationList local, remote, snapshot;
  const Spec ours{xp(1, 0), xp(1, 28), "ours", "my note"};
  const Spec gone{xp(1, 10), xp(1, 40), "theirs", "", OLD, NEWER, true};
  put(local, ours);
  put(remote, gone);
  MergeRun run;
  merge(local, remote, snapshot, run);
  ASSERT_EQ(run.merged.size(), 2u);
  EXPECT_FALSE(find(run.merged, keyOf(ours))->deleted);
  EXPECT_TRUE(find(run.merged, keyOf(gone))->deleted);
  EXPECT_EQ(run.report.notesRescued, 1);
  EXPECT_EQ(run.report.deletedLocally, 0);
}

TEST(AnnotationMerge, MergedNoteIsNotAppendedTwice) {
  AnnotationList local, remote, snapshot;
  const Spec ours{xp(1, 0), xp(1, 28), "ours", "T-Deck thought"};
  const Spec theirs{xp(1, 10), xp(1, 40), "theirs", "Boox thought", NEWER};
  put(local, ours);
  put(remote, theirs);
  MergeRun first;
  merge(local, remote, snapshot, first);
  ASSERT_EQ(first.report.notesMerged, 1);

  // The loser shows up again (e.g. another device still had it): no re-append.
  AnnotationList local2, remote2, snapshot2;
  put(local2, ours);
  ASSERT_FALSE(loadMap(remote2, first.upload).lossy);
  ASSERT_FALSE(loadMap(snapshot2, first.upload).lossy);
  MergeRun second;
  merge(local2, remote2, snapshot2, second, true, "2026-10-05 12:30:00");
  ASSERT_EQ(second.merged.size(), 1u);
  EXPECT_EQ(second.report.notesMerged, 0);
  EXPECT_EQ(find(second.merged, keyOf(theirs))->get(Field::Note), find(first.merged, keyOf(theirs))->get(Field::Note));
  EXPECT_FALSE(second.report.remoteChanged);

  // And a plain resync of the result is a no-op.
  AnnotationList local3, remote3, snapshot3;
  copyLocal(first.merged, local3);
  ASSERT_FALSE(loadMap(remote3, first.upload).lossy);
  ASSERT_FALSE(loadMap(snapshot3, first.upload).lossy);
  MergeRun third;
  merge(local3, remote3, snapshot3, third, true, "2026-10-05 12:30:00");
  EXPECT_FALSE(third.report.remoteChanged);
  EXPECT_FALSE(third.report.localChanged);
}

TEST(AnnotationMerge, TruncatedMergeIsNotAppendedTwice) {
  AnnotationList local, remote, snapshot;
  const Spec theirs{xp(1, 10), xp(1, 40), "theirs", std::string(MAX_NOTE_BYTES - 50, 'w'), NEWER};
  const Spec ours{xp(1, 0), xp(1, 28), "ours", std::string(200, 'l')};
  put(local, ours);
  put(remote, theirs);
  MergeRun first;
  merge(local, remote, snapshot, first);
  ASSERT_EQ(first.report.notesTruncated, 1);
  AnnotationList local2, remote2, snapshot2;
  put(local2, ours);
  ASSERT_FALSE(loadMap(remote2, first.upload).lossy);
  MergeRun second;
  merge(local2, remote2, snapshot2, second, true, "2026-10-05 12:30:00");
  EXPECT_EQ(second.report.notesMerged, 0);
  EXPECT_EQ(second.report.notesKeptApart, 0);
  EXPECT_FALSE(second.report.remoteChanged);
}

TEST(AnnotationMerge, KOReaderEntryKeepsItsShapeWhenANoteMergesIn) {
  // A KOReader-made remote entry (pageno, lighten, no datetime_updated) wins
  // and takes our note; everything else of it is untouched.
  const std::string json =
      R"({"/body/DocFragment[10]/body/div/p[1]/text().10||/body/DocFragment[10]/body/div/p[1]/text().40":)"
      R"({"datetime":"2026-10-04 11:00:00","page":"/body/DocFragment[10]/body/div/p[1]/text().10","color":"gray",)"
      R"("text":"theirs","pageno":15,"pos0":"/body/DocFragment[10]/body/div/p[1]/text().10",)"
      R"("pos1":"/body/DocFragment[10]/body/div/p[1]/text().40","chapter":"Prologue","drawer":"lighten",)"
      R"("note":"from KOReader"}})";
  AnnotationList local, remote, snapshot;
  ASSERT_FALSE(loadMap(remote, json).lossy);
  put(local, {xp(1, 0), xp(1, 28), "ours", "from the T-Deck"});
  MergeRun run;
  merge(local, remote, snapshot, run);
  ASSERT_EQ(run.merged.size(), 1u);
  const Annotation& w = run.merged[0];
  EXPECT_EQ(w.get(Field::Note), std::string("from KOReader") + std::string(MERGED_NOTE_MARKER) + "from the T-Deck");
  EXPECT_EQ(w.get(Field::Datetime), "2026-10-04 11:00:00");
  EXPECT_EQ(w.get(Field::DatetimeUpdated), NOW);
  EXPECT_EQ(w.get(Field::Drawer), "lighten");
  EXPECT_TRUE(w.hasPageno);
  EXPECT_EQ(w.pageno, 15);
  EXPECT_NE(run.upload.find(R"("datetime_updated":"2026-10-05 12:00:00")"), std::string::npos);
}

TEST(AnnotationMerge, MergedNoteWithoutCurrentClockIsUndated) {
  AnnotationList local, remote, snapshot;
  put(local, {xp(1, 0), xp(1, 28), "ours", "T-Deck thought"});
  const Spec theirs{xp(1, 10), xp(1, 40), "theirs", "Boox thought", NEWER};
  put(remote, theirs);
  MergeRun run;
  merge(local, remote, snapshot, run, false);
  const Annotation* w = find(run.merged, keyOf(theirs));
  ASSERT_NE(w, nullptr);
  EXPECT_TRUE(w->undated);
  EXPECT_NE(w->get(Field::Note).find("T-Deck thought"), std::string_view::npos);
}

// --- local flags sidecar --------------------------------------------------------

TEST(LocalFlags, RoundTripAndShape) {
  AnnotationList list;
  Spec undated{xp(1, 0), xp(1, 5)};
  undated.undated = true;
  Spec seed{xp(2, 0), xp(2, 5)};
  seed.localOnly = true;
  put(list, undated);
  put(list, seed);
  put(list, {xp(3, 0), xp(3, 5)});
  EXPECT_TRUE(hasLocalFlags(list));
  std::string flags;
  ASSERT_TRUE(writeLocalFlags(list, stringSink(flags)));
  EXPECT_EQ(flags, "{\"undated\":[\"" + std::string("\\/body\\/DocFragment[10]\\/body\\/div\\/p[1]\\/text().0||") +
                       "\\/body\\/DocFragment[10]\\/body\\/div\\/p[1]\\/text().5\"],\"local_only\":[\"" +
                       "\\/body\\/DocFragment[10]\\/body\\/div\\/p[2]\\/text().0||" +
                       "\\/body\\/DocFragment[10]\\/body\\/div\\/p[2]\\/text().5\"]}");
  // The main file stays AnnotationSync-shaped: no private keys.
  const std::string main = writeMap(list);
  EXPECT_EQ(main.find("undated"), std::string::npos);
  EXPECT_EQ(main.find("local"), std::string::npos);

  AnnotationList copy;
  copyLocal(list, copy);
  ASSERT_EQ(copy.size(), 3u);
  EXPECT_TRUE(find(copy, keyOf(undated))->undated);
  EXPECT_FALSE(find(copy, keyOf(undated))->localOnly);
  EXPECT_TRUE(find(copy, keyOf(seed))->localOnly);
  EXPECT_FALSE(copy[2].undated || copy[2].localOnly);
}

TEST(LocalFlags, UnknownKeysAndJunkAreHarmless) {
  AnnotationList list;
  put(list, {xp(1, 0), xp(1, 5)});
  LocalFlagsReader reader(list);
  ASSERT_TRUE(reader.begin());
  const std::string json = "{\"future\":{\"x\":[1,2]},\"undated\":[\"nope||nope\",3],\"local_only\":[]}";
  reader.feed(json.data(), json.size());
  size_t applied = 99;
  EXPECT_TRUE(reader.finish(&applied));
  EXPECT_EQ(applied, 0u);
  EXPECT_FALSE(list[0].undated);

  LocalFlagsReader broken(list);
  ASSERT_TRUE(broken.begin());
  broken.feed("{\"undated\":[", 12);
  EXPECT_FALSE(broken.finish());
}
