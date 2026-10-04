// DECKPOINT: per-word visible offsets survive layout, hyphenation and the section
// cache, and every laid-out word maps to a KOReader XPointer of the same character.

#include <Epub/Page.h>
#include <Epub/blocks/TextBlock.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "ChapterXPathResolver.h"
#include "CreDomReference.h"
#include "Epub/css/CssParser.h"
#include "Epub/parsers/ChapterHtmlSlimParser.h"

namespace {
namespace fs = std::filesystem;

constexpr char kIndented[] = R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.1//EN" "http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd">
<html xmlns="http://www.w3.org/1999/xhtml">
  <head>
    <title>Fixture</title>
  </head>
  <body>
    <section>
      <h1>Chapter   One</h1>
      <div class="intro">
        Loose text directly in a div,
        spanning several lines of the narrow test viewport.
        <p>First paragraph with <em>emphasis</em> and a
           <a href="#n1" id="r1">link</a>, then incomprehensibilities and extraordinarily
           long words that the hyphenator has to split across lines.</p>
        <p>Entities: caf&eacute; &amp; cr&egrave;me&nbsp;br&ucirc;l&eacute; &#8212; done.</p>
      </div>
      <img src="missing.png" alt="A missing picture"/>
      <ol>
        <li>Numbered item one</li>
        <li>Numbered <i>item</i> two</li>
      </ol>
      <ul><li>Bulleted item</li></ul>
      <p>
        Closing paragraph with leading whitespace and several more words to wrap.
      </p>
    </section>
  </body>
</html>
)";

struct Layout {
  std::vector<std::unique_ptr<Page>> pages;
  std::vector<const TextBlock*> lines() const {
    std::vector<const TextBlock*> out;
    for (const auto& page : pages) {
      for (const auto& element : page->elements) {
        if (element->getTag() != TAG_PageLine) continue;
        const auto* block = static_cast<const PageLine&>(*element).getBlock();
        if (block && block->valid() && block->wordCount() > 0) out.push_back(block);
      }
    }
    return out;
  }
};

Layout layOut(const std::string& xhtml, const bool hyphenation, const uint16_t width = 200,
              const uint16_t height = 160) {
  static int counter = 0;
  const fs::path dir = fs::temp_directory_path() / "deckpoint-word-offsets";
  fs::create_directories(dir);
  const auto path = (dir / ("doc" + std::to_string(counter++) + ".xhtml")).string();
  {
    std::ofstream out(path, std::ios::binary);
    out << xhtml;
  }
  CssParser cssParser{dir.string()};
  GfxRenderer renderer;
  Layout layout;
  ChapterHtmlSlimParser parser{
      nullptr,   path,
      renderer,  0,
      1.0f,      false,
      0,         width,
      height,    hyphenation,
      false,     [&](std::unique_ptr<Page> page, auto, auto, auto) {
                                 layout.pages.push_back(std::move(page)); },
      true,      "",
      "",        0,
      {},        nullptr,
      &cssParser};
  EXPECT_TRUE(parser.parseAndBuildPages());
  return layout;
}

std::u32string decode(const char* text) {
  std::u32string out;
  const auto* p = reinterpret_cast<const unsigned char*>(text);
  while (*p) out.push_back(utf8NextCodepoint(&p));
  return out;
}

bool sameChar(const char32_t laidOut, const char32_t source) {
  if (laidOut == source) return true;
  // No-break spaces become a " " token that glues its neighbours.
  return laidOut == ' ' && (source == 0x00A0 || source == 0x202F);
}

// The word's text must be the source text at its offset (an inserted hyphen excepted).
::testing::AssertionResult matchesSource(const TextBlock& block, const uint16_t i, const std::u32string& raw) {
  const uint32_t offset = block.wordVisibleOffset(i);
  std::u32string word = decode(block.wordText(i));
  if (offset >= raw.size()) return ::testing::AssertionFailure() << "offset " << offset << " past text";
  if (word.size() > 1 && word.back() == '-' &&
      (offset + word.size() - 1 >= raw.size() || raw[offset + word.size() - 1] != '-')) {
    word.pop_back();
  }
  for (size_t k = 0; k < word.size(); k++) {
    if (offset + k >= raw.size() || !sameChar(word[k], raw[offset + k])) {
      // NFD source text is composed to NFC for the fonts; compare composed forms.
      std::string src;
      for (size_t j = offset; j < std::min<size_t>(raw.size(), offset + word.size() * 3); j++) {
        utf8AppendCodepoint(raw[j], src);
      }
      const std::u32string composed = decode(utf8ComposeNfc(src).c_str());
      if (composed.compare(0, word.size(), word) == 0) return ::testing::AssertionSuccess();
      return ::testing::AssertionFailure()
             << "word '" << block.wordText(i) << "' at offset " << offset << " differs from source at +" << k;
    }
  }
  return ::testing::AssertionSuccess();
}

std::string readFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::vector<fs::path> chapterFiles(const fs::path& root) {
  std::vector<fs::path> out;
  std::error_code ec;
  if (root.empty() || !fs::exists(root, ec)) return out;
  for (const auto& entry : fs::recursive_directory_iterator(root, ec)) {
    const auto ext = entry.path().extension().string();
    if (entry.is_regular_file() && (ext == ".xhtml" || ext == ".html" || ext == ".htm")) out.push_back(entry.path());
  }
  std::sort(out.begin(), out.end());
  return out;
}

// Lays out each chapter, checks every word's offset against the source text and
// round-trips a sample of words through KOReader XPointers.
void checkChapters(const std::vector<fs::path>& files, const size_t xpointerSamples) {
  size_t words = 0;
  size_t roundTrips = 0;
  for (const auto& file : files) {
    const std::string xhtml = readFile(file);
    credomref::Dom dom;
    if (!dom.build(xhtml)) continue;
    const Layout layout = layOut(xhtml, true, 440, 760);
    std::vector<uint32_t> offsets;
    for (const TextBlock* block : layout.lines()) {
      for (uint16_t i = 0; i < block->wordCount(); i++) {
        if (!block->wordHasVisibleOffset(i)) continue;
        ASSERT_TRUE(matchesSource(*block, i, dom.rawText)) << file.filename();
        offsets.push_back(block->wordVisibleOffset(i));
        words++;
      }
    }
    if (offsets.empty()) continue;
    const auto epub = std::make_shared<Epub>(std::vector<std::string>{xhtml});
    const size_t step = std::max<size_t>(1, offsets.size() / xpointerSamples);
    for (size_t k = 0; k < offsets.size(); k += step) {
      const uint32_t offset = offsets[k];
      const std::string xp = ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, offset);
      const credomref::Node* node = nullptr;
      uint32_t at = 0;
      ASSERT_TRUE(dom.resolve(xp, node, at)) << file.filename() << " " << offset << " " << xp;
      ASSERT_EQ(credomref::Dom::rawFor(node, at), offset) << file.filename() << " " << xp;
      ASSERT_EQ(ChapterXPathResolver::findVisibleTextOffsetForXPath(epub, 0, xp), std::optional<uint32_t>(offset))
          << file.filename() << " " << xp;
      roundTrips++;
    }
  }
  std::cout << "  " << words << " laid-out words matched their source, " << roundTrips << " XPointer round trips in "
            << files.size() << " files\n";
}
}  // namespace

TEST(WordOffsets, LaidOutWordsPointAtTheirSourceText) {
  credomref::Dom dom;
  ASSERT_TRUE(dom.build(kIndented));
  const Layout layout = layOut(kIndented, false);
  const auto lines = layout.lines();
  ASSERT_GT(layout.pages.size(), 1u);
  size_t checked = 0;
  for (const TextBlock* block : lines) {
    for (uint16_t i = 0; i < block->wordCount(); i++) {
      if (!block->wordHasVisibleOffset(i)) continue;
      EXPECT_TRUE(matchesSource(*block, i, dom.rawText));
      checked++;
    }
  }
  EXPECT_GT(checked, 60u);
}

TEST(WordOffsets, MultiLineParagraphKeepsIncreasingOffsets) {
  const Layout layout = layOut(kIndented, false);
  uint32_t previous = 0;
  bool first = true;
  size_t lineCount = 0;
  for (const TextBlock* block : layout.lines()) {
    lineCount++;
    for (uint16_t i = 0; i < block->wordCount(); i++) {
      if (!block->wordHasVisibleOffset(i)) continue;
      const uint32_t offset = block->wordVisibleOffset(i);
      if (!first) {
        EXPECT_GT(offset, previous) << block->wordText(i);
      }
      previous = offset;
      first = false;
    }
    // The line base is the smallest offset on the line.
    uint32_t smallest = UINT32_MAX;
    for (uint16_t i = 0; i < block->wordCount(); i++) {
      if (block->wordHasVisibleOffset(i)) smallest = std::min(smallest, block->wordVisibleOffset(i));
    }
    if (smallest != UINT32_MAX) {
      EXPECT_EQ(block->lineVisibleOffset(), smallest);
    }
  }
  EXPECT_GT(lineCount, 15u);
}

TEST(WordOffsets, GeneratedTokensHaveNoSourceOffset) {
  credomref::Dom dom;
  ASSERT_TRUE(dom.build(kIndented));
  const Layout layout = layOut(kIndented, false);
  size_t markers = 0;
  size_t altWords = 0;
  for (const TextBlock* block : layout.lines()) {
    for (uint16_t i = 0; i < block->wordCount(); i++) {
      const std::string text = block->wordText(i);
      if (text == "1." || text == "2." || text == "\xe2\x80\xa2") {
        EXPECT_FALSE(block->wordHasVisibleOffset(i)) << text;
        // A marker reports the offset of the item text that follows it.
        ASSERT_LT(i + 1, block->wordCount());
        EXPECT_EQ(block->wordVisibleOffset(i), block->wordVisibleOffset(i + 1));
        markers++;
      }
      if (text.find("missing") != std::string::npos || text == "picture]") {
        EXPECT_FALSE(block->wordHasVisibleOffset(i)) << text;
        altWords++;
      }
    }
  }
  EXPECT_EQ(markers, 3u);
  EXPECT_GE(altWords, 1u);
}

TEST(WordOffsets, HyphenatedWordsMapBothHalvesIntoTheSourceWord) {
  credomref::Dom dom;
  ASSERT_TRUE(dom.build(kIndented));
  const Layout layout = layOut(kIndented, true, 136);
  const auto lines = layout.lines();
  size_t splits = 0;
  for (size_t l = 0; l + 1 < lines.size(); l++) {
    const TextBlock& line = *lines[l];
    const uint16_t last = line.wordCount() - 1;
    const std::u32string head = decode(line.wordText(last));
    if (head.size() < 2 || head.back() != '-' || !line.wordHasVisibleOffset(last)) continue;
    const uint32_t start = line.wordVisibleOffset(last);
    if (dom.rawText[start + head.size() - 1] == '-') continue;  // a real hyphen, not an inserted one
    const TextBlock& next = *lines[l + 1];
    ASSERT_TRUE(next.wordHasVisibleOffset(0));
    // The continuation starts right after the prefix's letters, inside the same source word.
    EXPECT_EQ(next.wordVisibleOffset(0), start + head.size() - 1) << line.wordText(last) << next.wordText(0);
    EXPECT_TRUE(matchesSource(line, last, dom.rawText));
    EXPECT_TRUE(matchesSource(next, 0, dom.rawText));
    splits++;
  }
  EXPECT_GE(splits, 3u);
}

TEST(WordOffsets, SerializationRoundTripKeepsOffsets) {
  const Layout layout = layOut(kIndented, true);
  const auto path = fs::temp_directory_path() / "deckpoint-word-offsets" / "blocks.bin";
  const auto lines = layout.lines();
  ASSERT_FALSE(lines.empty());
  {
    HalFile out;
    ASSERT_TRUE(out.open(path.c_str(), "wb"));
    for (const TextBlock* block : lines) ASSERT_TRUE(block->serialize(out));
  }
  HalFile in;
  ASSERT_TRUE(in.open(path.c_str(), "rb"));
  for (const TextBlock* original : lines) {
    const auto copy = TextBlock::deserialize(in);
    ASSERT_NE(copy, nullptr);
    ASSERT_EQ(copy->wordCount(), original->wordCount());
    EXPECT_EQ(copy->lineVisibleOffset(), original->lineVisibleOffset());
    for (uint16_t i = 0; i < original->wordCount(); i++) {
      EXPECT_STREQ(copy->wordText(i), original->wordText(i));
      EXPECT_EQ(copy->wordXpos(i), original->wordXpos(i));
      EXPECT_EQ(copy->wordHasVisibleOffset(i), original->wordHasVisibleOffset(i));
      EXPECT_EQ(copy->wordVisibleOffset(i), original->wordVisibleOffset(i));
    }
  }
}

TEST(WordOffsets, DirectConstructionAndFallbacks) {
  const std::vector<std::string> words{"\xe2\x80\xa2", "alpha", "beta", "x"};
  const std::vector<int16_t> xpos{0, 10, 60, 100};
  const std::vector<EpdFontFamily::Style> styles(4, EpdFontFamily::REGULAR);
  const std::vector<uint16_t> deltas{TextBlock::NO_VISIBLE_OFFSET, 0, 6, TextBlock::NO_VISIBLE_OFFSET};
  TextBlock block(words, xpos, styles, {}, {}, BlockStyle(), {}, {}, 1000, deltas);
  ASSERT_TRUE(block.valid());
  EXPECT_EQ(block.lineVisibleOffset(), 1000u);
  EXPECT_FALSE(block.wordHasVisibleOffset(0));
  EXPECT_EQ(block.wordVisibleOffset(0), 1000u);  // next word with a source offset
  EXPECT_EQ(block.wordVisibleOffset(1), 1000u);
  EXPECT_EQ(block.wordVisibleOffset(2), 1006u);
  EXPECT_EQ(block.wordVisibleOffset(3), 1006u);  // trailing marker: previous word

  TextBlock without(words, xpos, styles, {}, {}, BlockStyle());
  ASSERT_TRUE(without.valid());
  EXPECT_FALSE(without.wordHasVisibleOffset(1));

  TextBlock mismatched(words, xpos, styles, {}, {}, BlockStyle(), {}, {}, 0, std::vector<uint16_t>{1, 2});
  EXPECT_FALSE(mismatched.valid());
}

TEST(WordOffsets, LaidOutWordsRoundTripThroughXPointers) {
  const auto dir = fs::temp_directory_path() / "deckpoint-word-offsets" / "fixture";
  fs::create_directories(dir);
  {
    std::ofstream out(dir / "fixture.xhtml", std::ios::binary);
    out << kIndented;
  }
  checkChapters({dir / "fixture.xhtml"}, 1000);
}

TEST(WordOffsets, BundledTestEpubChapters) {
#ifdef DECKPOINT_TEST_EPUB_XHTML_DIR
  const auto files = chapterFiles(DECKPOINT_TEST_EPUB_XHTML_DIR);
  if (files.empty()) GTEST_SKIP() << "test EPUBs were not extracted at configure time";
  checkChapters(files, 60);
#else
  GTEST_SKIP() << "test EPUB extraction not configured";
#endif
}

TEST(WordOffsets, ExtraChaptersFromEnvironment) {
  const char* dir = std::getenv("DECKPOINT_XHTML_DIR");
  if (!dir || !*dir) GTEST_SKIP() << "set DECKPOINT_XHTML_DIR to an extracted EPUB";
  const auto files = chapterFiles(dir);
  if (files.empty()) GTEST_SKIP() << "no chapters under " << dir;
  checkChapters(files, 40);
}
