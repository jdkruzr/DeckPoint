// DECKPOINT: visible offset <-> KOReader XPointer agreement on real-world XHTML.
// Expectations follow crengine's DOM text model (see CreDomReference.h for sources).

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "ChapterXPathResolver.h"
#include "CreDomReference.h"

namespace {
using KOReaderXPointer::Bias;
using KOReaderXPointer::Style;

std::shared_ptr<Epub> epubWith(std::string xhtml) {
  std::vector<std::string> spine;
  spine.push_back(std::move(xhtml));
  return std::make_shared<Epub>(std::move(spine));
}

// Pretty-printed XHTML as EPUB producers emit it: indentation everywhere, text
// directly in a <div>, headings, inline elements, entities and a no-break space.
constexpr char kIndented[] = R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.1//EN" "http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd">
<html xmlns="http://www.w3.org/1999/xhtml">
  <head>
    <title>Fixture</title>
    <style>p { margin: 0 }</style>
  </head>
  <body>
    <section>
      <h1>Chapter   One</h1>
      <div class="intro">
        Loose text directly in a div,
        spanning lines.
        <p>First paragraph with <em>emphasis</em> and a
           <a href="#n1" id="r1">link</a>.</p>
        <p>Entities: caf&eacute; &amp; cr&egrave;me&nbsp;br&ucirc;l&eacute; &#8212; done.</p>
      </div>
      <p>
        Leading whitespace paragraph
      </p>
      <blockquote><p>Quoted <span>nested <b>bold</b></span> tail</p></blockquote>
      <ul>
        <li>Item one</li>
        <li>Item <i>two</i></li>
      </ul>
    </section>
  </body>
</html>
)";

uint32_t rawOffsetOf(const credomref::Dom& dom, const std::u32string& needle, size_t nth = 0) {
  size_t pos = 0;
  for (size_t i = 0;; i++) {
    pos = dom.rawText.find(needle, i == 0 ? 0 : pos + 1);
    if (pos == std::u32string::npos || i == nth) break;
  }
  return pos == std::u32string::npos ? UINT32_MAX : static_cast<uint32_t>(pos);
}

credomref::Dom domOf(const std::string& xhtml) {
  credomref::Dom dom;
  EXPECT_TRUE(dom.build(xhtml));
  return dom;
}

bool isSpace(const char32_t c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// Every non-whitespace character must map to the XPointer of that same character
// and back, and range ends must land just past it.
void expectRoundTripAt(const std::shared_ptr<Epub>& epub, const credomref::Dom& dom, const uint32_t raw,
                       const std::string& label) {
  const std::string xp = ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, raw);
  ASSERT_FALSE(xp.empty()) << label << " raw=" << raw;
  const credomref::Node* node = nullptr;
  uint32_t offset = 0;
  ASSERT_TRUE(dom.resolve(xp, node, offset)) << label << " unresolvable " << xp;
  EXPECT_EQ(credomref::Dom::rawFor(node, offset), raw) << label << " " << xp;
  const auto back = ChapterXPathResolver::findVisibleTextOffsetForXPath(epub, 0, xp);
  ASSERT_TRUE(back.has_value()) << label << " reverse failed " << xp;
  EXPECT_EQ(*back, raw) << label << " " << xp;

  const std::string endXp = ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, raw + 1, Bias::End);
  ASSERT_TRUE(dom.resolve(endXp, node, offset)) << label << " unresolvable end " << endXp;
  ASSERT_GT(offset, 0u) << label << " " << endXp;
  EXPECT_EQ(credomref::Dom::rawFor(node, offset - 1), raw) << label << " end " << endXp;
}

std::string readFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::vector<std::filesystem::path> chapterFiles(const std::filesystem::path& root) {
  std::vector<std::filesystem::path> out;
  std::error_code ec;
  if (root.empty() || !std::filesystem::exists(root, ec)) return out;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
    const auto ext = entry.path().extension().string();
    if (entry.is_regular_file() && (ext == ".xhtml" || ext == ".html" || ext == ".htm")) out.push_back(entry.path());
  }
  std::sort(out.begin(), out.end());
  return out;
}

// Samples up to `maxSamples` word starts per chapter (the positions highlights and
// page starts use) and round-trips each one.
void roundTripChapters(const std::vector<std::filesystem::path>& files, const size_t maxSamples) {
  size_t checked = 0;
  for (const auto& file : files) {
    const std::string xhtml = readFile(file);
    credomref::Dom dom;
    if (!dom.build(xhtml)) continue;  // not well-formed XML; the layout skips it too
    std::vector<uint32_t> starts;
    for (uint32_t i = 0; i < dom.rawText.size(); i++) {
      if (!isSpace(dom.rawText[i]) && (i == 0 || isSpace(dom.rawText[i - 1]))) starts.push_back(i);
    }
    if (starts.empty()) continue;
    const auto epub = epubWith(xhtml);
    const size_t step = std::max<size_t>(1, starts.size() / maxSamples);
    for (size_t i = 0; i < starts.size(); i += step) {
      expectRoundTripAt(epub, dom, starts[i], file.filename().string());
      checked++;
      if (::testing::Test::HasFailure()) return;
    }
  }
  std::cout << "  round-tripped " << checked << " word starts in " << files.size() << " files\n";
}
}  // namespace

TEST(KOReaderXPointer, TextDirectlyInDivUsesTheDivTextNode) {
  const auto epub = epubWith(kIndented);
  const auto dom = domOf(kIndented);
  // The div's first text node keeps one collapsed leading space: 'L' is char 1.
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, rawOffsetOf(dom, U"Loose")),
            "/body/DocFragment[1]/body/section[1]/div[1]/text()[1].1");
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, rawOffsetOf(dom, U"spanning")),
            "/body/DocFragment[1]/body/section[1]/div[1]/text()[1].31");
}

TEST(KOReaderXPointer, HeadingWhitespaceCollapses) {
  const auto epub = epubWith(kIndented);
  const auto dom = domOf(kIndented);
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, rawOffsetOf(dom, U"One")),
            "/body/DocFragment[1]/body/section[1]/h1[1]/text()[1].8");
}

TEST(KOReaderXPointer, InlineElementsSplitTextNodes) {
  const auto epub = epubWith(kIndented);
  const auto dom = domOf(kIndented);
  const std::string p = "/body/DocFragment[1]/body/section[1]/div[1]/p[1]";
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, rawOffsetOf(dom, U"First")),
            p + "/text()[1].0");
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, rawOffsetOf(dom, U"emphasis")),
            p + "/em[1]/text()[1].0");
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, rawOffsetOf(dom, U"and a")),
            p + "/text()[2].1");
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, rawOffsetOf(dom, U"link")),
            p + "/a[1]/text()[1].0");
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, rawOffsetOf(dom, U"link") + 4),
            p + "/text()[3].0");
}

TEST(KOReaderXPointer, EntitiesCountAsOneCharacter) {
  const auto epub = epubWith(kIndented);
  const auto dom = domOf(kIndented);
  const std::string p = "/body/DocFragment[1]/body/section[1]/div[1]/p[2]";
  // "Entities: café & crème brûlé — done." -- the no-break space is not collapsible.
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, rawOffsetOf(dom, U"brûl")),
            p + "/text()[1].23");
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, rawOffsetOf(dom, U"done")),
            p + "/text()[1].31");
}

TEST(KOReaderXPointer, LeadingWhitespaceOfAParagraphCollapsesToOneSpace) {
  const auto epub = epubWith(kIndented);
  const auto dom = domOf(kIndented);
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, rawOffsetOf(dom, U"Leading")),
            "/body/DocFragment[1]/body/section[1]/p[1]/text()[1].1");
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, rawOffsetOf(dom, U"paragraph", 1)),
            "/body/DocFragment[1]/body/section[1]/p[1]/text()[1].20");
}

TEST(KOReaderXPointer, NestedInlineAndListItems) {
  const auto epub = epubWith(kIndented);
  const auto dom = domOf(kIndented);
  const std::string q = "/body/DocFragment[1]/body/section[1]/blockquote[1]/p[1]";
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, rawOffsetOf(dom, U"bold")),
            q + "/span[1]/b[1]/text()[1].0");
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, rawOffsetOf(dom, U"tail")),
            q + "/text()[2].1");
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, rawOffsetOf(dom, U"two")),
            "/body/DocFragment[1]/body/section[1]/ul[1]/li[2]/i[1]/text()[1].0");
}

TEST(KOReaderXPointer, WhitespaceBetweenInlineSiblingsIsATextNode) {
  const std::string doc =
      "<html><body>\n  <p><em>a</em> <em>b</em>\n  c</p>\n  <div>\n    <p>x</p>\n    loose\n    <p>y</p>\n  "
      "</div>\n</body></html>";
  const auto epub = epubWith(doc);
  const auto dom = domOf(doc);
  // p: em, " ", em, " c" -> "c" is text()[2]; div: p, " loose ", p (edge whitespace removed).
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, rawOffsetOf(dom, U"c")),
            "/body/DocFragment[1]/body/p[1]/text()[2].1");
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, rawOffsetOf(dom, U"loose")),
            "/body/DocFragment[1]/body/div[1]/text()[1].1");
}

TEST(KOReaderXPointer, RoundTripsEveryCharacterOfIndentedFixture) {
  const auto epub = epubWith(kIndented);
  const auto dom = domOf(kIndented);
  ASSERT_FALSE(dom.rawText.empty());
  for (uint32_t raw = 0; raw < dom.rawText.size(); raw++) {
    if (isSpace(dom.rawText[raw])) continue;
    expectRoundTripAt(epub, dom, raw, "indented");
    if (HasFailure()) return;
  }
}

TEST(KOReaderXPointer, WhitespaceOffsetsMoveToTheNextKeptCharacter) {
  const auto epub = epubWith(kIndented);
  const auto dom = domOf(kIndented);
  // The indentation between </h1> and <div> is dropped by crengine.
  const uint32_t afterHeading = rawOffsetOf(dom, U"One") + 3;
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, afterHeading),
            "/body/DocFragment[1]/body/section[1]/div[1]/text()[1].1");
}

TEST(KOReaderXPointer, RangeEndsUseTheEndBias) {
  const auto epub = epubWith(kIndented);
  const auto dom = domOf(kIndented);
  const uint32_t link = rawOffsetOf(dom, U"link");
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, link + 4, Bias::End),
            "/body/DocFragment[1]/body/section[1]/div[1]/p[1]/a[1]/text()[1].4");
}

TEST(KOReaderXPointer, LegacyStyleOmitsIndexesOfOnlyChildren) {
  const auto epub = epubWith(kIndented);
  const auto dom = domOf(kIndented);
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, rawOffsetOf(dom, U"bold"), Bias::Start,
                                                                Style::Legacy),
            "/body/DocFragment/body/section/blockquote/p/span/b/text().0");
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, rawOffsetOf(dom, U"First"), Bias::Start,
                                                                Style::Legacy),
            "/body/DocFragment/body/section/div/p[1]/text()[1].0");
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, rawOffsetOf(dom, U"Leading"), Bias::Start,
                                                                Style::Legacy),
            "/body/DocFragment/body/section/p/text().1");
}

TEST(KOReaderXPointer, ExplicitStyleIndexesEveryStep) {
  const auto epub = epubWith(kIndented);
  const auto dom = domOf(kIndented);
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, rawOffsetOf(dom, U"bold"), Bias::Start,
                                                                Style::Explicit),
            "/body[1]/DocFragment[1]/body[1]/section[1]/blockquote[1]/p[1]/span[1]/b[1]/text()[1].0");
}

TEST(KOReaderXPointer, ReverseAcceptsEveryStyle) {
  const auto epub = epubWith(kIndented);
  const auto dom = domOf(kIndented);
  const uint32_t bold = rawOffsetOf(dom, U"bold");
  for (const char* xp : {"/body/DocFragment/body/section/blockquote/p/span/b/text().0",
                         "/body[1]/DocFragment[1]/body[1]/section[1]/blockquote[1]/p[1]/span[1]/b[1]/text()[1].0",
                         "/body/DocFragment[1]/body/section[1]/blockquote[1]/p[1]/span[1]/b[1]/text()[1].0"}) {
    const auto offset = ChapterXPathResolver::findVisibleTextOffsetForXPath(epub, 0, xp);
    ASSERT_TRUE(offset.has_value()) << xp;
    EXPECT_EQ(*offset, bold) << xp;
  }
}

TEST(KOReaderXPointer, ReverseResolvesElementPointsToElementStart) {
  const std::string doc = "<html><body><p>one</p><div><img src=\"a.png\"/>two</div></body></html>";
  const auto epub = epubWith(doc);
  const auto offset =
      ChapterXPathResolver::findVisibleTextOffsetForXPath(epub, 0, "/body/DocFragment[1]/body/div/img.0");
  ASSERT_TRUE(offset.has_value());
  EXPECT_EQ(*offset, 3u);
}

TEST(KOReaderXPointer, ReverseRelaxedFirstStepFindsWrappedPath) {
  const std::string doc = "<html><body><div><p>one</p><p>two</p></div></body></html>";
  const auto epub = epubWith(doc);
  EXPECT_FALSE(ChapterXPathResolver::findVisibleTextOffsetForXPath(epub, 0, "/body/DocFragment[1]/body/p[2]/text().1")
                   .has_value());
  const auto offset =
      ChapterXPathResolver::findVisibleTextOffsetForXPath(epub, 0, "/body/DocFragment[1]/body/p[2]/text().1", true);
  ASSERT_TRUE(offset.has_value());
  EXPECT_EQ(*offset, 4u);
}

TEST(KOReaderXPointer, PreservesWhitespaceInPre) {
  const std::string doc = "<html><body><pre>\nline one\n\tx  y</pre></body></html>";
  const auto epub = epubWith(doc);
  const auto dom = domOf(doc);
  // Leading newline after <pre> is stripped; the tab expands to 8 columns.
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, rawOffsetOf(dom, U"x")),
            "/body/DocFragment[1]/body/pre[1]/text()[1].17");
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, rawOffsetOf(dom, U"y")),
            "/body/DocFragment[1]/body/pre[1]/text()[1].20");
}

TEST(KOReaderXPointer, RoundTripsBundledTestEpubChapters) {
#ifdef DECKPOINT_TEST_EPUB_XHTML_DIR
  const auto files = chapterFiles(DECKPOINT_TEST_EPUB_XHTML_DIR);
  if (files.empty()) GTEST_SKIP() << "test EPUBs were not extracted at configure time";
  roundTripChapters(files, 200);
#else
  GTEST_SKIP() << "test EPUB extraction not configured";
#endif
}

TEST(KOReaderXPointer, RoundTripsExtraChaptersFromEnvironment) {
  // DECKPOINT_XHTML_DIR=<extracted EPUB dir> runs the property over a real book.
  const char* dir = std::getenv("DECKPOINT_XHTML_DIR");
  if (!dir || !*dir) GTEST_SKIP() << "set DECKPOINT_XHTML_DIR to an extracted EPUB";
  const auto files = chapterFiles(dir);
  if (files.empty()) GTEST_SKIP() << "no chapters under " << dir;
  roundTripChapters(files, 150);
}

namespace {
std::u32string toU32(const std::string& s) {
  std::u32string out;
  const auto* p = reinterpret_cast<const unsigned char*>(s.c_str());
  while (*p) out.push_back(utf8NextCodepoint(&p));
  return out;
}

// KOReader's selection text joins blocks with '\n' and collapses whitespace.
std::u32string collapsed(const std::u32string& s) {
  std::u32string out;
  for (const char32_t c : s) {
    if (isSpace(c)) {
      if (!out.empty() && out.back() != U' ') out.push_back(U' ');
    } else {
      out.push_back(c);
    }
  }
  while (!out.empty() && out.back() == U' ') out.pop_back();
  return out;
}

std::vector<std::string> splitTabs(const std::string& line) {
  std::vector<std::string> parts;
  std::stringstream ss(line);
  std::string part;
  while (std::getline(ss, part, '\t')) parts.push_back(part);
  return parts;
}
}  // namespace

TEST(KOReaderXPointer, MatchesRealKOReaderAnnotations) {
  // DECKPOINT_KOREADER_ANNOTATIONS=<tsv: spine file, DocFragment index, pos0, pos1, text>
  // exported from a KOReader metadata.epub.lua; files are relative to DECKPOINT_XHTML_DIR.
  const char* tsv = std::getenv("DECKPOINT_KOREADER_ANNOTATIONS");
  const char* dir = std::getenv("DECKPOINT_XHTML_DIR");
  if (!tsv || !*tsv || !dir || !*dir) GTEST_SKIP() << "set DECKPOINT_KOREADER_ANNOTATIONS and DECKPOINT_XHTML_DIR";
  std::ifstream in(tsv);
  std::string line;
  size_t checked = 0;
  while (std::getline(in, line)) {
    const auto f = splitTabs(line);
    if (f.size() < 5) continue;
    const int fragment = std::stoi(f[1]);
    const std::string xhtml = readFile(std::filesystem::path(dir) / f[0]);
    credomref::Dom dom;
    ASSERT_TRUE(dom.build(xhtml)) << f[0];
    std::vector<std::string> spine(fragment - 1);
    spine.push_back(xhtml);
    const auto epub = std::make_shared<Epub>(std::move(spine));
    const int spineIndex = fragment - 1;

    const auto start = ChapterXPathResolver::findVisibleTextOffsetForXPath(epub, spineIndex, f[2]);
    const auto end = ChapterXPathResolver::findVisibleTextOffsetForXPath(epub, spineIndex, f[3]);
    ASSERT_TRUE(start.has_value()) << "pos0 unresolved: " << f[2];
    ASSERT_TRUE(end.has_value()) << "pos1 unresolved: " << f[3];
    ASSERT_LE(*start, *end) << f[2];

    std::string expectedText = f[4];
    for (size_t p; (p = expectedText.find("\\n")) != std::string::npos;) expectedText.replace(p, 2, " ");
    EXPECT_EQ(collapsed(dom.rawText.substr(*start, *end - *start)), collapsed(toU32(expectedText)))
        << f[2] << " .. " << f[3];

    EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, spineIndex, *start, Bias::Start, Style::Legacy),
              f[2]);
    EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, spineIndex, *end, Bias::End, Style::Legacy),
              f[3]);
    checked++;
  }
  std::cout << "  checked " << checked << " KOReader annotations\n";
  EXPECT_GT(checked, 0u);
}
