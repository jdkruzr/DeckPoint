// DECKPOINT: annotation model, AnnotationSync JSON shape, caps and the
// per-page underline plan.

#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "deckpoint/annotations/Annotation.h"
#include "deckpoint/annotations/AnnotationGeometry.h"
#include "deckpoint/annotations/AnnotationJson.h"
#include "deckpoint/annotations/AnnotationList.h"

using namespace deckpoint::annotations;

namespace {

std::string readFixture(const char* name) {
  std::ifstream in(std::string(DECKPOINT_ANNOTATION_FIXTURES) + "/" + name, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

LoadReport load(AnnotationList& list, const std::string& json, const size_t chunk = 512) {
  AnnotationJsonReader reader(list);
  EXPECT_TRUE(reader.begin());
  for (size_t i = 0; i < json.size(); i += chunk) reader.feed(json.data() + i, std::min(chunk, json.size() - i));
  return reader.finish();
}

std::string write(const AnnotationList& list) {
  std::string out;
  const JsonSink sink{&out, [](void* ctx, const char* data, size_t len) {
                        static_cast<std::string*>(ctx)->append(data, len);
                        return true;
                      }};
  EXPECT_TRUE(writeAnnotationMap(list, sink));
  return out;
}

Annotation makeHighlight(const std::string& pos0, const std::string& pos1, const std::string& text = "words",
                         const std::string& note = "") {
  Annotation a;
  std::string_view v[FIELD_COUNT];
  v[static_cast<size_t>(Field::Pos0)] = pos0;
  v[static_cast<size_t>(Field::Pos1)] = pos1;
  v[static_cast<size_t>(Field::Page)] = pos0;
  v[static_cast<size_t>(Field::Text)] = text;
  v[static_cast<size_t>(Field::Note)] = note;
  v[static_cast<size_t>(Field::Drawer)] = OWN_DRAWER;
  v[static_cast<size_t>(Field::Color)] = OWN_COLOR;
  v[static_cast<size_t>(Field::Datetime)] = "2026-10-04 10:00:00";
  EXPECT_TRUE(a.assign(v));
  return a;
}

constexpr const char* P1_0 = "/body/DocFragment[10]/body/div/p[1]/text().0";
constexpr const char* P1_28 = "/body/DocFragment[10]/body/div/p[1]/text().28";

}  // namespace

// --- model -------------------------------------------------------------------

TEST(Annotation, KeyIsPos0BarBarPos1) {
  const Annotation a = makeHighlight(P1_0, P1_28);
  EXPECT_EQ(annotationKey(a), std::string(P1_0) + "||" + P1_28);
  EXPECT_TRUE(a.isHighlight());
}

TEST(Annotation, BookmarkKey) {
  Annotation a;
  std::string_view v[FIELD_COUNT];
  v[static_cast<size_t>(Field::Page)] = "/body/DocFragment[3]/body/p[2].0";
  ASSERT_TRUE(a.assign(v));
  EXPECT_FALSE(a.isHighlight());
  EXPECT_EQ(annotationKey(a), "BOOKMARK|/body/DocFragment[3]/body/p[2].0");
}

TEST(Annotation, SetRebuildsOneFieldAndKeepsOthers) {
  Annotation a = makeHighlight(P1_0, P1_28, "text");
  ASSERT_TRUE(a.set(Field::Note, "a note\nline two"));
  EXPECT_EQ(a.get(Field::Note), "a note\nline two");
  EXPECT_EQ(a.get(Field::Pos0), P1_0);
  EXPECT_EQ(a.get(Field::Text), "text");
  // Self-assignment from a view into the block.
  ASSERT_TRUE(a.set(Field::Chapter, a.get(Field::Text)));
  EXPECT_EQ(a.get(Field::Chapter), "text");
  EXPECT_EQ(a.get(Field::Chapter).data()[4], '\0');  // views are C strings
}

// --- JSON: the real AnnotationSync file (synthetic text) ---------------------

TEST(AnnotationJson, LoadsRealAnnotationSyncFile) {
  AnnotationList list;
  const LoadReport report = load(list, readFixture("koreader_remote.json"));
  EXPECT_FALSE(report.parseError);
  EXPECT_FALSE(report.lossy);
  EXPECT_FALSE(list.readOnly());
  EXPECT_EQ(report.loaded, 6u);
  EXPECT_EQ(report.keyMismatches, 0u);
  ASSERT_EQ(list.size(), 6u);

  const int prologue = list.find(std::string(P1_0) + "||" + P1_28);
  ASSERT_GE(prologue, 0);
  const Annotation& a = list[static_cast<size_t>(prologue)];
  EXPECT_EQ(a.get(Field::Chapter), "Prologue");
  EXPECT_EQ(a.get(Field::Drawer), "lighten");
  EXPECT_EQ(a.get(Field::Color), "gray");
  EXPECT_EQ(a.get(Field::Datetime), "2026-10-04 02:50:02");
  EXPECT_FALSE(a.has(Field::DatetimeUpdated));  // only present after an edit
  EXPECT_EQ(a.get(Field::Page), P1_0);
  EXPECT_TRUE(a.hasPageno);
  EXPECT_EQ(a.pageno, 15);
  EXPECT_EQ(a.spineIndex, 9);
  EXPECT_FALSE(a.deleted);

  // The edited one carries a note with a real newline and datetime_updated.
  const int noted =
      list.find("/body/DocFragment[10]/body/div/p[5]/text().30||/body/DocFragment[10]/body/div/p[5]/text().74");
  ASSERT_GE(noted, 0);
  const Annotation& n = list[static_cast<size_t>(noted)];
  EXPECT_EQ(n.get(Field::DatetimeUpdated), "2026-10-04 02:51:42");
  EXPECT_NE(n.get(Field::Note).find('\n'), std::string_view::npos);
  EXPECT_EQ(n.get(Field::Note),
            "Test note with \"quotes\" \\ and /slash\nsecond line \xC3\xA9\xE2\x80\x94\xF0\x9F\x98\x80");

  // Cross-paragraph highlight: pos0 and pos1 in different text nodes.
  const int cross =
      list.find("/body/DocFragment[10]/body/div/p[3]/text()[2].224||/body/DocFragment[10]/body/div/p[4]/text().8");
  ASSERT_GE(cross, 0);
  EXPECT_EQ(list[static_cast<size_t>(cross)].get(Field::Text), "end of a paragraph.\xE2\x80\x9D\nNext words");

  EXPECT_TRUE(list.hasHighlightsIn(9));
  EXPECT_TRUE(list.hasHighlightsIn(170));
  EXPECT_FALSE(list.hasHighlightsIn(10));
}

TEST(AnnotationJson, RoundTripIsStableAndKeepsEveryField) {
  AnnotationList first;
  load(first, readFixture("koreader_remote.json"));
  const std::string written = write(first);

  // KOReader-style output: escaped slashes, pos0||pos1 keys, raw UTF-8.
  EXPECT_NE(written.find("\"\\/body\\/DocFragment[10]\\/body\\/div\\/p[1]\\/text().0||"
                         "\\/body\\/DocFragment[10]\\/body\\/div\\/p[1]\\/text().28\":{"),
            std::string::npos);
  EXPECT_NE(written.find("\"pageno\":1700"), std::string::npos);
  EXPECT_NE(written.find("\\nsecond line"), std::string::npos);
  EXPECT_NE(written.find("\xE2\x80\x9C"), std::string::npos);  // “ stays UTF-8
  EXPECT_EQ(written.find("datetime_updated\":\"\""), std::string::npos);

  AnnotationList second;
  const LoadReport report = load(second, written);
  EXPECT_FALSE(report.lossy);
  ASSERT_EQ(second.size(), first.size());
  for (size_t i = 0; i < first.size(); i++) {
    for (size_t f = 0; f < FIELD_COUNT; f++) {
      EXPECT_EQ(first[i].get(static_cast<Field>(f)), second[i].get(static_cast<Field>(f))) << i << "/" << f;
    }
    EXPECT_EQ(first[i].pageno, second[i].pageno);
    EXPECT_EQ(first[i].hasPageno, second[i].hasPageno);
    EXPECT_EQ(first[i].deleted, second[i].deleted);
  }
  EXPECT_EQ(write(second), written);
}

TEST(AnnotationJson, ByteAtATimeFeedMatchesChunked) {
  AnnotationList chunked;
  AnnotationList bytewise;
  const std::string json = readFixture("koreader_remote.json");
  load(chunked, json, 512);
  load(bytewise, json, 1);
  EXPECT_EQ(write(chunked), write(bytewise));
}

TEST(AnnotationJson, TombstonePreserved) {
  const std::string json = std::string("{\"") + P1_0 + "||" + P1_28 + "\":{\"pos0\":\"" + P1_0 + "\",\"pos1\":\"" +
                           P1_28 +
                           "\",\"deleted\":true,\"datetime\":\"2026-10-01 09:00:00\","
                           "\"datetime_updated\":\"2026-10-04 12:00:00\"}}";
  AnnotationList list;
  const LoadReport report = load(list, json);
  EXPECT_FALSE(report.lossy);
  ASSERT_EQ(list.size(), 1u);
  EXPECT_TRUE(list[0].deleted);
  EXPECT_FALSE(list.hasHighlightsIn(9));  // never drawn
  const std::string out = write(list);
  EXPECT_NE(out.find("\"deleted\":true"), std::string::npos);
  EXPECT_NE(out.find("\"datetime_updated\":\"2026-10-04 12:00:00\""), std::string::npos);
}

TEST(AnnotationJson, EscapingRoundTrip) {
  AnnotationList list;
  const std::string note = std::string("line1\nline2\t\"q\" back\\slash /sl \x01 ctl \xC3\xA9 \xF0\x9F\x98\x80");
  ASSERT_EQ(list.add(makeHighlight(P1_0, P1_28, "t", note)), AddResult::Added);
  const std::string out = write(list);
  EXPECT_NE(out.find("line1\\nline2\\t\\\"q\\\" back\\\\slash \\/sl \\u0001 ctl"), std::string::npos);
  AnnotationList back;
  EXPECT_FALSE(load(back, out).lossy);
  ASSERT_EQ(back.size(), 1u);
  EXPECT_EQ(back[0].get(Field::Note), note);
}

TEST(AnnotationJson, DecodesUnicodeEscapes) {
  const std::string json = std::string("{\"") + P1_0 + "||" + P1_28 + "\":{\"pos0\":\"" + P1_0 + "\",\"pos1\":\"" +
                           P1_28 + "\",\"note\":\"caf\\u00e9 \\ud83d\\ude00\"}}";
  AnnotationList list;
  load(list, json);
  ASSERT_EQ(list.size(), 1u);
  EXPECT_EQ(list[0].get(Field::Note), "caf\xC3\xA9 \xF0\x9F\x98\x80");
}

TEST(AnnotationJson, BookmarksAndNumericPagePassThrough) {
  const std::string json =
      "{\"BOOKMARK|\\/body\\/DocFragment[3]\\/body\\/p[2].0\":{\"page\":\"\\/body\\/DocFragment[3]\\/body\\/p[2].0\","
      "\"datetime\":\"2026-10-01 09:00:00\",\"text\":\"bm\"},"
      "\"BOOKMARK|42\":{\"page\":42,\"datetime\":\"2026-10-01 09:00:00\"}}";
  AnnotationList list;
  const LoadReport report = load(list, json);
  EXPECT_FALSE(report.lossy);
  ASSERT_EQ(list.size(), 2u);
  EXPECT_FALSE(list[0].isHighlight());
  EXPECT_TRUE(list[1].pageIsNumber);
  const std::string out = write(list);
  EXPECT_NE(out.find("\"BOOKMARK|\\/body\\/DocFragment[3]\\/body\\/p[2].0\":{"), std::string::npos);
  EXPECT_NE(out.find("\"BOOKMARK|42\":{\"datetime\":\"2026-10-01 09:00:00\",\"page\":42}"), std::string::npos);
  EXPECT_FALSE(list.hasHighlightsIn(2));
}

TEST(AnnotationJson, UnknownFieldsAreCountedNotFatal) {
  const std::string json = std::string("{\"") + P1_0 + "||" + P1_28 + "\":{\"pos0\":\"" + P1_0 + "\",\"pos1\":\"" +
                           P1_28 + "\",\"future\":\"x\",\"nested\":{\"a\":[1,2]},\"text\":\"kept\"}}";
  AnnotationList list;
  const LoadReport report = load(list, json);
  EXPECT_FALSE(report.lossy);
  EXPECT_GE(report.unknownFields, 2u);
  ASSERT_EQ(list.size(), 1u);
  EXPECT_EQ(list[0].get(Field::Text), "kept");
}

TEST(AnnotationJson, OversizedValueMakesListReadOnly) {
  const std::string huge(3000, 'x');  // over StreamingJsonParser's token buffer
  const std::string json = std::string("{\"a||b\":{\"pos0\":\"a\",\"pos1\":\"b\",\"note\":\"") + huge + "\"},\"" +
                           P1_0 + "||" + P1_28 + "\":{\"pos0\":\"" + P1_0 + "\",\"pos1\":\"" + P1_28 + "\"}}";
  AnnotationList list;
  const LoadReport report = load(list, json);
  EXPECT_TRUE(report.lossy);
  EXPECT_EQ(report.skipped, 1u);
  EXPECT_EQ(report.loaded, 1u);
  EXPECT_TRUE(list.readOnly());
  EXPECT_EQ(list.add(makeHighlight("/x", "/y")), AddResult::ReadOnly);
}

TEST(AnnotationJson, MalformedFileIsReadOnly) {
  AnnotationList list;
  const LoadReport report = load(list, "{\"a||b\":{\"pos0\":\"a\",\"pos1\":");
  EXPECT_TRUE(report.parseError);
  EXPECT_TRUE(list.readOnly());
}

TEST(AnnotationJson, EmptyListWritesEmptyObject) {
  AnnotationList list;
  EXPECT_EQ(write(list), "{}");
}

// --- caps ----------------------------------------------------------------------

TEST(AnnotationList, TextAndNoteCaps) {
  AnnotationList list;
  EXPECT_EQ(list.add(makeHighlight(P1_0, P1_28, std::string(MAX_TEXT_BYTES + 1, 'a'))), AddResult::TooLong);
  EXPECT_EQ(list.add(makeHighlight(P1_0, P1_28, "t", std::string(MAX_NOTE_BYTES + 1, 'n'))), AddResult::TooLong);
  EXPECT_EQ(list.add(makeHighlight(P1_0, P1_28, std::string(MAX_TEXT_BYTES, 'a'), std::string(MAX_NOTE_BYTES, 'n'))),
            AddResult::Added);
}

TEST(AnnotationList, CountCapAndReplace) {
  AnnotationList list(16 * 1024 * 1024);
  for (size_t i = 0; i < AnnotationList::MAX_ANNOTATIONS; i++) {
    const std::string p0 = "/body/DocFragment[1]/body/p[" + std::to_string(i + 1) + "]/text().0";
    ASSERT_EQ(list.add(makeHighlight(p0, p0 + "1")), AddResult::Added) << i;
  }
  EXPECT_EQ(list.add(makeHighlight("/body/DocFragment[2]/body/p[1]/text().0", "/x")), AddResult::TooMany);
  // Same key replaces even at the cap.
  const std::string p0 = "/body/DocFragment[1]/body/p[7]/text().0";
  EXPECT_EQ(list.add(makeHighlight(p0, p0 + "1", "edited")), AddResult::Replaced);
  EXPECT_EQ(list.size(), AnnotationList::MAX_ANNOTATIONS);
  EXPECT_EQ(list[6].get(Field::Text), "edited");
}

TEST(AnnotationList, TombstoneStampsUpdateAndWrites) {
  AnnotationList list;
  ASSERT_EQ(list.add(makeHighlight(P1_0, P1_28, "words", "a note")), AddResult::Added);
  const size_t before = list.blobBytes();
  EXPECT_FALSE(list.tombstone(1, "2026-10-05 08:00:00"));  // out of range
  ASSERT_TRUE(list.tombstone(0, "2026-10-05 08:00:00"));
  EXPECT_TRUE(list[0].deleted);
  EXPECT_EQ(list[0].get(Field::DatetimeUpdated), "2026-10-05 08:00:00");
  EXPECT_EQ(list[0].get(Field::Note), "a note");  // the rest is kept for the merge
  EXPECT_EQ(list.blobBytes(), before + strlen("2026-10-05 08:00:00"));
  EXPECT_FALSE(list.hasHighlightsIn(9));
  const std::string out = write(list);
  EXPECT_NE(out.find("\"deleted\":true"), std::string::npos);
  EXPECT_NE(out.find("\"datetime_updated\":\"2026-10-05 08:00:00\""), std::string::npos);
}

TEST(AnnotationList, TombstoneRefusedWhenReadOnly) {
  AnnotationList list;
  ASSERT_EQ(list.add(makeHighlight(P1_0, P1_28)), AddResult::Added);
  list.markReadOnly();
  EXPECT_FALSE(list.tombstone(0, "2026-10-05 08:00:00"));
  EXPECT_FALSE(list[0].deleted);
}

TEST(AnnotationList, RamBudget) {
  AnnotationList list(400);
  EXPECT_EQ(list.add(makeHighlight(P1_0, P1_28, std::string(100, 'a'))), AddResult::Added);
  EXPECT_EQ(list.add(makeHighlight("/body/DocFragment[2]/body/p[1]/text().0", "/y", std::string(300, 'b'))),
            AddResult::OverBudget);
  EXPECT_LE(list.blobBytes(), 400u);
}

TEST(AnnotationList, OverBudgetFileLoadIsReadOnly) {
  AnnotationList list(300);
  const LoadReport report = load(list, readFixture("koreader_remote.json"));
  EXPECT_TRUE(report.lossy);
  EXPECT_EQ(report.firstFailure, AddResult::OverBudget);
  EXPECT_TRUE(list.readOnly());
}

// --- geometry ----------------------------------------------------------------

TEST(AnnotationGeometry, SpineFromXPointer) {
  EXPECT_EQ(spineFromXPointer("/body/DocFragment[10]/body/div/p[1]/text().0"), 9);
  EXPECT_EQ(spineFromXPointer("/body[1]/DocFragment[171]/body[1]/div[1]/p[66]/text()[1].283"), 170);
  EXPECT_EQ(spineFromXPointer("/body/DocFragment[1]"), 0);
  EXPECT_EQ(spineFromXPointer("/body/DocFragment[0]/body"), -1);
  EXPECT_EQ(spineFromXPointer("/body/DocFragment[x]/body"), -1);
  EXPECT_EQ(spineFromXPointer("/html/DocFragment[3]/body"), -1);
  EXPECT_EQ(spineFromXPointer("BOOKMARK|42"), -1);
  EXPECT_EQ(spineFromXPointer(""), -1);
}

TEST(AnnotationGeometry, RangesOverlapIsHalfOpen) {
  EXPECT_TRUE(rangesOverlap(0, 5, 4, 10));
  EXPECT_FALSE(rangesOverlap(0, 5, 5, 10));  // pos1 is exclusive
  EXPECT_FALSE(rangesOverlap(5, 10, 0, 5));
  EXPECT_TRUE(rangesOverlap(3, 4, 0, 10));
}

TEST(AnnotationGeometry, WordSourceLength) {
  EXPECT_EQ(wordSourceLength("word", 4), 4u);
  EXPECT_EQ(wordSourceLength("caf\xC3\xA9", 5), 4u);
  EXPECT_EQ(wordSourceLength("\xE2\x80\x83Indent", 9), 6u);
  EXPECT_EQ(wordSourceLength("", 0), 1u);
}

namespace {
struct MeasureLog {
  std::vector<size_t> calls;
  int width = 10;
};
int measureFixed(void* ctx, const size_t index) {
  auto* log = static_cast<MeasureLog*>(ctx);
  log->calls.push_back(index);
  return log->width;
}
constexpr MarkStyle STYLE{12, 2, 3, 2, 1};
}  // namespace

TEST(AnnotationGeometry, UnderlineSpansMergePerLine) {
  // Two lines, three words each, 20 px apart; words are 10 px wide.
  const PageWord words[] = {
      {100, 105, 0, 0, 0},  {106, 110, 20, 0, 0},  {111, 115, 40, 0, 0},
      {116, 120, 0, 30, 1}, {121, 125, 20, 30, 1}, {126, 130, 40, 30, 1},
  };
  const HighlightRange ranges[] = {{107, 123, false}};  // 2nd word .. 5th word
  MeasureLog log;
  std::vector<MarkRect> out;
  planPageMarks(words, 6, ranges, 1, STYLE, &measureFixed, &log, out);
  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(out[0].x, 20);
  EXPECT_EQ(out[0].w, 30);  // words 2-3, gap included
  EXPECT_EQ(out[0].y, 12);
  EXPECT_EQ(out[0].h, 2);
  EXPECT_EQ(out[1].x, 0);
  EXPECT_EQ(out[1].w, 30);
  EXPECT_EQ(out[1].y, 42);
  // Only highlighted words are measured.
  EXPECT_EQ(log.calls, (std::vector<size_t>{1, 2, 3, 4}));
}

TEST(AnnotationGeometry, PartialWordUnderlinesWholeWord) {
  const PageWord words[] = {{0, 6, 0, 0, 0}, {7, 12, 20, 0, 0}};
  const HighlightRange ranges[] = {{3, 4, false}};
  MeasureLog log;
  std::vector<MarkRect> out;
  planPageMarks(words, 2, ranges, 1, STYLE, &measureFixed, &log, out);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].x, 0);
  EXPECT_EQ(out[0].w, 10);
}

TEST(AnnotationGeometry, NoteMarkerAfterLastWordWhenHighlightEndsOnPage) {
  const PageWord words[] = {{0, 4, 0, 0, 0}, {5, 9, 20, 0, 0}, {9, 10, 30, 0, 0}, {11, 15, 40, 0, 0}};
  // Ends at 9, before trailing punctuation (word 3 starts at 9).
  const HighlightRange ranges[] = {{0, 9, true}};
  MeasureLog log;
  std::vector<MarkRect> out;
  planPageMarks(words, 4, ranges, 1, STYLE, &measureFixed, &log, out);
  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(out[1].x, 20 + 10 + 1);
  EXPECT_EQ(out[1].y, 2);
  EXPECT_EQ(out[1].w, 3);
}

TEST(AnnotationGeometry, NoNoteMarkerWhenHighlightContinuesOnNextPage) {
  const PageWord words[] = {{0, 4, 0, 0, 0}, {5, 9, 20, 0, 0}};
  const HighlightRange ranges[] = {{5, 50, true}};
  MeasureLog log;
  std::vector<MarkRect> out;
  planPageMarks(words, 2, ranges, 1, STYLE, &measureFixed, &log, out);
  ASSERT_EQ(out.size(), 1u);  // underline only
}

TEST(AnnotationGeometry, NothingHighlightedDrawsNothing) {
  const PageWord words[] = {{0, 4, 0, 0, 0}};
  const HighlightRange ranges[] = {{10, 20, true}};
  MeasureLog log;
  std::vector<MarkRect> out;
  planPageMarks(words, 1, ranges, 1, STYLE, &measureFixed, &log, out);
  EXPECT_TRUE(out.empty());
  EXPECT_TRUE(log.calls.empty());
}

TEST(AnnotationGeometry, TimestampFormat) {
  std::tm t{};
  t.tm_year = 2026 - 1900;
  t.tm_mon = 9;
  t.tm_mday = 4;
  t.tm_hour = 2;
  t.tm_min = 5;
  t.tm_sec = 9;
  char out[20];
  formatTimestamp(t, out);
  EXPECT_STREQ(out, "2026-10-04 02:05:09");
  EXPECT_EQ(strlen(UNSET_TIMESTAMP), MAX_DATETIME_BYTES);
}
