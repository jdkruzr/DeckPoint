// DECKPOINT: notes list order and `:export` -- XPointer order, file names,
// the Markdown file, Kindle My Clippings blocks and the streaming filter that
// replaces one book's blocks in an existing clippings file.

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "deckpoint/annotations/Annotation.h"
#include "deckpoint/annotations/AnnotationExport.h"
#include "deckpoint/annotations/AnnotationGeometry.h"
#include "deckpoint/annotations/AnnotationList.h"

using namespace deckpoint::annotations;

namespace {

JsonSink stringSink(std::string& out) {
  return JsonSink{&out, [](void* ctx, const char* data, size_t len) {
                    static_cast<std::string*>(ctx)->append(data, len);
                    return true;
                  }};
}

Annotation makeHighlight(const std::string& pos0, const std::string& text = "words", const std::string& note = "") {
  const std::string pos1 = pos0 + "9";
  Annotation a;
  std::string_view v[FIELD_COUNT];
  v[static_cast<size_t>(Field::Pos0)] = pos0;
  v[static_cast<size_t>(Field::Pos1)] = pos1;
  v[static_cast<size_t>(Field::Page)] = pos0;
  v[static_cast<size_t>(Field::Text)] = text;
  v[static_cast<size_t>(Field::Note)] = note;
  v[static_cast<size_t>(Field::Datetime)] = "2026-10-04 10:00:00";
  EXPECT_TRUE(a.assign(v));
  return a;
}

struct VecSource {
  std::vector<ExportEntry> entries;
  EntrySource source() {
    return EntrySource{this, entries.size(), [](void* ctx, size_t i, ExportEntry& out) {
                         out = static_cast<VecSource*>(ctx)->entries[i];
                         return true;
                       }};
  }
};

ExportEntry entry(const char* text, const char* note, const char* chapter, int chapterNumber, int percent,
                  uint32_t location = 0, int32_t page = 0, const char* datetime = "2026-10-04 02:50:02") {
  ExportEntry e;
  e.text = text;
  e.note = note;
  e.chapter = chapter;
  e.datetime = datetime;
  e.chapterNumber = chapterNumber;
  e.percent = percent;
  e.location = location;
  e.page = page;
  return e;
}

std::string filter(const std::string& input, const std::string& title, size_t chunk, size_t* dropped = nullptr) {
  std::string out;
  ClippingsFilter f(title, stringSink(out));
  for (size_t i = 0; i < input.size(); i += chunk) {
    EXPECT_TRUE(f.feed(input.data() + i, std::min(chunk, input.size() - i)));
  }
  EXPECT_TRUE(f.finish());
  if (dropped) *dropped = f.droppedBlocks();
  return out;
}

std::string sanitize(const std::string& name) {
  char out[MAX_FILE_NAME_BYTES + 1];
  sanitizeFileName(name, out, sizeof(out));
  return out;
}

}  // namespace

// ------------------------------------------------------------------ order

TEST(NotesOrder, XPointersCompareNumerically) {
  EXPECT_LT(compareXPointers("/body/DocFragment[2]/body/p[2]/text().0", "/body/DocFragment[2]/body/p[10]/text().0"), 0);
  EXPECT_LT(compareXPointers("/body/DocFragment[2]/body/p[1]/text().9", "/body/DocFragment[2]/body/p[1]/text().10"), 0);
  EXPECT_EQ(compareXPointers("/body/DocFragment[2]/body/p/text().5", "/body/DocFragment[2]/body/p[1]/text()[1].5"), 0);
  EXPECT_GT(compareXPointers("/body/DocFragment[10]/body/p[1]/text().0", "/body/DocFragment[9]/body/p[3]/text().0"), 0);
  // An element comes before a point inside it, an ancestor before its descendants.
  EXPECT_LT(compareXPointers("/body/DocFragment[2]/body/div[2]", "/body/DocFragment[2]/body/div[2]/p[1]/text().0"), 0);
  EXPECT_LT(compareXPointers("/body/DocFragment[2]/body/div[2]/p[3]", "/body/DocFragment[2]/body/div[2]/p[3].4"), 0);
  // Same depth, different names: a stable, antisymmetric order.
  const char* h = "/body/DocFragment[2]/body/h2/text().0";
  const char* p = "/body/DocFragment[2]/body/p[1]/text().0";
  EXPECT_EQ(compareXPointers(h, p), -compareXPointers(p, h));
  EXPECT_NE(compareXPointers(h, p), 0);
}

TEST(NotesOrder, BrowseOrderSortsLiveHighlightsBySpineThenPosition) {
  AnnotationList list;
  ASSERT_EQ(list.adopt(makeHighlight("/body/DocFragment[3]/body/p[10]/text().0", "c10")), AddResult::Added);
  ASSERT_EQ(list.adopt(makeHighlight("/body/DocFragment[12]/body/p[1]/text().0", "l1")), AddResult::Added);
  ASSERT_EQ(list.adopt(makeHighlight("/body/DocFragment[3]/body/p[2]/text().30", "c2b")), AddResult::Added);
  ASSERT_EQ(list.adopt(makeHighlight("/body/DocFragment[3]/body/p[2]/text().4", "c2a")), AddResult::Added);
  ASSERT_EQ(list.adopt(makeHighlight("/body/DocFragment[3]/body/p[5]/text().0", "gone")), AddResult::Added);
  ASSERT_TRUE(list.tombstone(4, "2026-10-05 00:00:00"));
  // A bookmark entry (no pos0/pos1) is not listed.
  Annotation bookmark;
  std::string_view v[FIELD_COUNT];
  v[static_cast<size_t>(Field::Page)] = "/body/DocFragment[3]/body/p[1]";
  ASSERT_TRUE(bookmark.assign(v));
  ASSERT_EQ(list.adopt(std::move(bookmark)), AddResult::Added);

  std::vector<uint16_t> order;
  browseOrder(list, order);
  std::vector<std::string> texts;
  for (const uint16_t i : order) texts.emplace_back(list[i].get(Field::Text));
  EXPECT_EQ(texts, (std::vector<std::string>{"c2a", "c2b", "c10", "l1"}));
}

// ------------------------------------------------------------------ file names

TEST(NotesExport, SanitizesFileNamesForFat) {
  EXPECT_EQ(sanitize("Red Rising"), "Red Rising");
  EXPECT_EQ(sanitize("Star Wars: A New Hope"), "Star Wars A New Hope");
  EXPECT_EQ(sanitize("a/b\\c*d?e\"f<g>h|i"), "a b c d e f g h i");
  EXPECT_EQ(sanitize("  ..Hidden.  "), "Hidden");
  EXPECT_EQ(sanitize("Line\nbreak\ttab"), "Line break tab");
  EXPECT_EQ(sanitize(""), "Untitled");
  EXPECT_EQ(sanitize("???"), "Untitled");
  EXPECT_EQ(sanitize("Ends with dots..."), "Ends with dots");
  // Cut to MAX_FILE_NAME_BYTES without splitting a code point (é = 2 bytes).
  std::string longName(MAX_FILE_NAME_BYTES - 1, 'a');
  longName += "\xC3\xA9tude";
  const std::string cut = sanitize(longName);
  EXPECT_EQ(cut, std::string(MAX_FILE_NAME_BYTES - 1, 'a'));
  // Room for the whole name: kept as is.
  EXPECT_EQ(sanitize("\xC3\x89tude"), "\xC3\x89tude");
}

// ------------------------------------------------------------------ Markdown

TEST(NotesExport, MarkdownGroupsChaptersAndKeepsNoteLines) {
  VecSource src;
  src.entries.push_back(
      entry("I would have lived in peace.", "First line\nsecond line\n\nNew paragraph", "Prologue", 1, 2));
  src.entries.push_back(entry("Break the chains.", "", "Prologue", 1, 3, 0, 0, UNSET_TIMESTAMP));
  src.entries.push_back(entry("Line one\nLine two", "", "Chapter 1", 2, 10));
  src.entries.push_back(entry("Untitled part", "", "", 7, -1, 0, 0, ""));

  const ExportBook book{"Red \"Rising\"", "Pierce Brown", "0123abcd", "2026-10-04 12:34:56"};
  std::string out;
  ASSERT_TRUE(writeMarkdown(book, src.source(), stringSink(out)));
  const std::string expected =
      "---\n"
      "title: \"Red \\\"Rising\\\"\"\n"
      "author: \"Pierce Brown\"\n"
      "exported: 2026-10-04 12:34:56\n"
      "koreader_id: 0123abcd\n"
      "highlights: 4\n"
      "source: DeckPoint\n"
      "---\n"
      "\n"
      "# Red \"Rising\"\n"
      "\n"
      "*Pierce Brown*\n"
      "\n"
      "## Prologue\n"
      "\n"
      "> I would have lived in peace.\n"
      "\n"
      "First line\n"
      "second line\n"
      "\n"
      "New paragraph\n"
      "\n"
      "*\xE2\x80\x94 Ch. 1, 2% \xC2\xB7 2026-10-04 02:50*\n"
      "\n"
      "> Break the chains.\n"
      "\n"
      "*\xE2\x80\x94 Ch. 1, 3%*\n"
      "\n"
      "## Chapter 1\n"
      "\n"
      "> Line one\n"
      "> Line two\n"
      "\n"
      "*\xE2\x80\x94 Ch. 2, 10% \xC2\xB7 2026-10-04 02:50*\n"
      "\n"
      "## Chapter 7\n"
      "\n"
      "> Untitled part\n"
      "\n"
      "*\xE2\x80\x94 Ch. 7*\n";
  EXPECT_EQ(out, expected);
}

TEST(NotesExport, MarkdownEscapesLeadingMarkers) {
  VecSource src;
  src.entries.push_back(entry("# Not a heading\n> not nested\n- not a list\n1. not numbered\n  * star\nplain #",
                              "## note", "Ch", 1, 5, 0, 0, ""));
  const ExportBook book{"T", "", "id", ""};
  std::string out;
  ASSERT_TRUE(writeMarkdown(book, src.source(), stringSink(out)));
  EXPECT_NE(out.find("> \\# Not a heading\n> \\> not nested\n> \\- not a list\n> 1\\. not numbered\n> \\* star\n"
                     "> plain #\n"),
            std::string::npos)
      << out;
  EXPECT_NE(out.find("\n\\## note\n"), std::string::npos) << out;
  // No author: no author line; no export date: no "exported:" line.
  EXPECT_EQ(out.find("exported:"), std::string::npos);
  EXPECT_NE(out.find("author: \"\"\n"), std::string::npos);
  EXPECT_EQ(out.find("\n*\n"), std::string::npos);
}

// ------------------------------------------------------------------ My Clippings

TEST(NotesExport, KindleDates) {
  char out[48];
  ASSERT_TRUE(formatKindleDate("2026-10-04 02:50:02", out, sizeof(out)));
  EXPECT_STREQ(out, "Sunday, October 4, 2026 2:50:02 AM");
  ASSERT_TRUE(formatKindleDate("2000-02-29 12:00:00", out, sizeof(out)));
  EXPECT_STREQ(out, "Tuesday, February 29, 2000 12:00:00 PM");
  ASSERT_TRUE(formatKindleDate("2024-12-31 00:05:09", out, sizeof(out)));
  EXPECT_STREQ(out, "Tuesday, December 31, 2024 12:05:09 AM");
  EXPECT_FALSE(formatKindleDate(UNSET_TIMESTAMP, out, sizeof(out)));
  EXPECT_STREQ(out, "");
  EXPECT_FALSE(formatKindleDate("2026-13-01 00:00:00", out, sizeof(out)));
  EXPECT_FALSE(formatKindleDate("yesterday", out, sizeof(out)));
}

TEST(NotesExport, ClippingsTitleLine) {
  char out[CLIPPINGS_TITLE_MAX + 1];
  EXPECT_EQ(clippingsTitleLine(ExportBook{"Red Rising", "Pierce Brown", "", ""}, out, sizeof(out)), 25u);
  EXPECT_STREQ(out, "Red Rising (Pierce Brown)");
  clippingsTitleLine(ExportBook{" Title\n", "", "", ""}, out, sizeof(out));
  EXPECT_STREQ(out, "Title");
  // Cut at CLIPPINGS_TITLE_MAX, never inside a code point.
  std::string longTitle(CLIPPINGS_TITLE_MAX - 1, 'x');
  longTitle += "\xC3\xA9";
  EXPECT_EQ(clippingsTitleLine(ExportBook{longTitle, "A", "", ""}, out, sizeof(out)), CLIPPINGS_TITLE_MAX - 1);
}

TEST(NotesExport, ClippingsBlocks) {
  VecSource src;
  src.entries.push_back(entry("I would have lived in peace.", "", "Prologue", 1, 2, 345, 12));
  src.entries.push_back(entry("Two\nlines", "My note\n==========", "Ch", 2, 9, 0, 0, UNSET_TIMESTAMP));
  std::string out;
  ASSERT_TRUE(writeClippings("Red Rising (Pierce Brown)", src.source(), stringSink(out)));
  const std::string expected =
      "Red Rising (Pierce Brown)\r\n"
      "- Your Highlight on page 12 | Location 345 | Added on Sunday, October 4, 2026 2:50:02 AM\r\n"
      "\r\n"
      "I would have lived in peace.\r\n"
      "==========\r\n"
      "Red Rising (Pierce Brown)\r\n"
      "- Your Highlight on Location 1\r\n"
      "\r\n"
      "Two\r\n"
      "lines\r\n"
      "==========\r\n"
      "Red Rising (Pierce Brown)\r\n"
      "- Your Note on Location 1\r\n"
      "\r\n"
      "My note\r\n"
      "========== \r\n"
      "==========\r\n";
  EXPECT_EQ(out, expected);
}

namespace {

const std::string OTHER_BLOCK =
    "Dune (Frank Herbert)\r\n"
    "- Your Highlight on page 4 | Location 50-51 | Added on Monday, January 1, 2024 9:00:00 AM\r\n"
    "\r\n"
    "Fear is the mind-killer.\r\n"
    "==========\r\n";
const std::string OUR_BLOCK =
    "Red Rising (Pierce Brown)\r\n"
    "- Your Highlight on Location 9\r\n"
    "\r\n"
    "old text\r\n"
    "==========\r\n";

}  // namespace

TEST(ClippingsFilter, DropsOnlyThisBooksBlocks) {
  const std::string input = "\xEF\xBB\xBF" + OUR_BLOCK + OTHER_BLOCK + OUR_BLOCK + OTHER_BLOCK + OUR_BLOCK;
  for (const size_t chunk : {size_t{1}, size_t{3}, size_t{7}, size_t{1024}}) {
    size_t dropped = 0;
    EXPECT_EQ(filter(input, "Red Rising (Pierce Brown)", chunk, &dropped), OTHER_BLOCK + OTHER_BLOCK)
        << "chunk " << chunk;
    EXPECT_EQ(dropped, 3u);
  }
}

TEST(ClippingsFilter, KeepsEverythingElseByteForByte) {
  // LF line ends, a BOM on a kept first block, an empty block, a separator-like
  // body line, no final newline.
  const std::string input =
      "\xEF\xBB\xBFOther (A)\n- meta\n\ntext\n= not a separator\n===========\n==========\n==========\n"
      "Red Rising (Pierce Brown) 2\n- meta\n\nkept: different title\n==========\nTail (B)\n- meta\n\nno newline";
  for (const size_t chunk : {size_t{1}, size_t{5}, size_t{4096}}) {
    size_t dropped = 9;
    EXPECT_EQ(filter(input, "Red Rising (Pierce Brown)", chunk, &dropped), input) << "chunk " << chunk;
    EXPECT_EQ(dropped, 0u);
  }
}

TEST(ClippingsFilter, LongHeaderLinesPassThrough) {
  const std::string longTitle(ClippingsFilter::HEADER_MAX + 50, 'L');
  const std::string block = longTitle + "\r\n- meta\r\n\r\nbody\r\n==========\r\n";
  EXPECT_EQ(filter(block + OUR_BLOCK + block, "Red Rising (Pierce Brown)", 1), block + block);
  EXPECT_EQ(filter(block + OUR_BLOCK + block, "Red Rising (Pierce Brown)", 64), block + block);
}

TEST(ClippingsFilter, UnterminatedLastHeaderIsDroppedWhenOurs) {
  EXPECT_EQ(filter(OTHER_BLOCK + "Red Rising (Pierce Brown)", "Red Rising (Pierce Brown)", 4), OTHER_BLOCK);
  EXPECT_EQ(filter("", "Red Rising (Pierce Brown)", 4), "");
}

TEST(ClippingsFilter, ExportTwiceDoesNotDuplicate) {
  VecSource src;
  src.entries.push_back(entry("new text", "a note", "Ch", 1, 5, 10));
  std::string ours;
  ASSERT_TRUE(writeClippings("Red Rising (Pierce Brown)", src.source(), stringSink(ours)));

  // First export onto a file with another book; then again onto the result.
  std::string first = filter(OTHER_BLOCK, "Red Rising (Pierce Brown)", 16) + ours;
  std::string second = filter(first, "Red Rising (Pierce Brown)", 16) + ours;
  EXPECT_EQ(first, OTHER_BLOCK + ours);
  EXPECT_EQ(second, first);
}
