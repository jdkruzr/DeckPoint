#include <Epub/Page.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "src/activities/settings/TextSettingsPreview.h"
#include "src/util/ParagraphIndentMigration.h"

#define class struct
#define private public
#include "Epub/parsers/ChapterHtmlSlimParser.h"
#undef private
#undef class

namespace {

class ChapterHtmlSlimParserTest : public ::testing::TestWithParam<const char*> {
 protected:
  std::string filepath = "unused.xhtml";
  GfxRenderer renderer;
  CssParser cssParser{"/tmp"};
  ChapterHtmlSlimParser parser{nullptr,
                               filepath,
                               renderer,
                               0,
                               1.0f,
                               false,
                               0,
                               static_cast<uint16_t>(renderer.getScreenWidth()),
                               static_cast<uint16_t>(renderer.getScreenHeight()),
                               false,
                               false,
                               {},
                               true,
                               "",
                               "",
                               0,
                               {},
                               nullptr,
                               &cssParser};

  void SetUp() override { parser.currentTextBlock = std::make_unique<ParsedText>(); }
};

TEST_F(ChapterHtmlSlimParserTest, RubySurvivesPartialParagraphExtraction) {
  ParsedText text;
  text.addWord("a", EpdFontFamily::REGULAR);
  text.addWord("b", EpdFontFamily::REGULAR);
  text.addWord("c", EpdFontFamily::REGULAR);
  text.setRubyForWordAt(2, "c");
  size_t lines = 0;
  text.layoutAndExtractLines(
      renderer, 0, 20,
      [&](std::unique_ptr<TextBlock> line, auto) {
        ++lines;
        EXPECT_TRUE(line->getRubyTexts().empty());
      },
      false);
  EXPECT_EQ(lines, 1u);
  const size_t retainedWords = text.size();
  ASSERT_GT(retainedWords, 0u);
  ASSERT_LT(retainedWords, 3u);
  text.layoutAndExtractLines(renderer, 0, 200, [&](std::unique_ptr<TextBlock> line, auto) {
    ++lines;
    ASSERT_EQ(line->getRubyTexts().size(), retainedWords);
    EXPECT_EQ(line->getRubyTexts().back(), "c");
    for (size_t i = 0; i + 1 < retainedWords; ++i) EXPECT_TRUE(line->getRubyTexts()[i].empty());
  });
  EXPECT_EQ(lines, 2u);
}

TEST_F(ChapterHtmlSlimParserTest, UnequalTableCellsAndRubySurvivePageBreaks) {
  parser.viewportWidth = 240;
  parser.viewportHeight = 32;
  parser.tableRowCells.reserve(2);
  std::multiset<std::string> expected;
  for (int column = 0; column < 2; ++column) {
    auto cell = std::make_unique<ParsedText>();
    for (int index = 0; index < (column == 0 ? 30 : 3); ++index) {
      const auto word = std::string(column == 0 ? "left" : "right") + std::to_string(index);
      expected.insert(word);
      cell->addWord(word, EpdFontFamily::REGULAR);
    }
    if (column == 0) cell->setRubyGroupAt(0, 2, "reading");
    parser.tableRowCells.push_back(std::move(cell));
  }
  std::multiset<std::string> actual;
  unsigned pages = 0;
  unsigned rubyLines = 0;
  auto inspect = [&](std::unique_ptr<Page> page, auto, auto, auto) {
    ++pages;
    for (const auto& element : page->elements) {
      if (element->getTag() != TAG_PageLine) continue;
      const auto& line = static_cast<const PageLine&>(*element);
      const auto& block = *line.getBlock();
      ASSERT_TRUE(block.valid());
      EXPECT_LE(element->yPos + 16 + block.getRubyShift(12), parser.viewportHeight);
      rubyLines += block.hasRuby();
      for (uint16_t word = 0; word < block.wordCount(); ++word) actual.insert(block.wordText(word));
    }
  };
  parser.completePageFn = inspect;
  parser.finishTableRow();
  ASSERT_NE(parser.currentPage, nullptr);
  inspect(std::move(parser.currentPage), 0, 0, 0);
  EXPECT_GT(pages, 2u);
  EXPECT_EQ(rubyLines, 1u);
  EXPECT_EQ(actual, expected);
  for (const auto& lines : parser.tableCellLines) EXPECT_TRUE(lines.empty());
}

TEST_F(ChapterHtmlSlimParserTest, PageImageDeserializeRejectsMissingImageBlock) {
  const auto path = std::filesystem::temp_directory_path() / "crosspoint-missing-image-cache.bin";
  {
    HalFile output;
    ASSERT_TRUE(output.open(path.c_str(), "wb"));
    const int16_t coordinates[] = {0, 0};
    output.write(coordinates, sizeof(coordinates));
  }
  HalFile input;
  ASSERT_TRUE(input.open(path.c_str(), "rb"));
  EXPECT_EQ(PageImage::deserialize(input), nullptr);
}

TEST_P(ChapterHtmlSlimParserTest, KeepsCssVerticalAlignAndInternalLinkMetadata) {
  const char* verticalAlign = GetParam();
  const char* expectedHref = "#note-target";
  const XML_Char* attributes[] = {"href", expectedHref, "style", verticalAlign, nullptr};

  ChapterHtmlSlimParser::startElement(&parser, "a", attributes);
  const uint8_t linkId = parser.currentFootnoteLinkId;
  ASSERT_NE(linkId, 0u);
  ChapterHtmlSlimParser::characterData(&parser, "1", 1);
  ChapterHtmlSlimParser::endElement(&parser, "a");

  ASSERT_EQ(parser.currentTextBlock->size(), 1u);
  const auto style = parser.currentTextBlock->getWordStyleAt(0);
  const auto expectedStyle =
      std::string(verticalAlign).find("super") != std::string::npos ? EpdFontFamily::SUP : EpdFontFamily::SUB;
  EXPECT_NE(static_cast<uint8_t>(style) & static_cast<uint8_t>(expectedStyle), 0u);

  ASSERT_EQ(parser.pendingFootnotes.size(), 1u);
  const FootnoteEntry& footnote = parser.pendingFootnotes.front().second;
  EXPECT_STREQ(footnote.href, expectedHref);
  ASSERT_EQ(parser.currentTextBlock->wordLinkIds.size(), 1u);
  EXPECT_EQ(parser.currentTextBlock->wordLinkIds.front(), linkId);
  EXPECT_TRUE(parser.currentTextBlock->linkTargetMatches(linkId, expectedHref));
}

INSTANTIATE_TEST_SUITE_P(CssVerticalAlign, ChapterHtmlSlimParserTest,
                         ::testing::Values("vertical-align: super", "vertical-align: sub"));

TEST_F(ChapterHtmlSlimParserTest, ParagraphWithHiddenAttributeShouldBeSkipped) {
  const XML_Char* attributes[] = {"hidden", "hidden", nullptr};

  parser.beginParse();
  ChapterHtmlSlimParser::startElement(&parser, "p", attributes);
  ChapterHtmlSlimParser::characterData(&parser, "[HIDDEN]", 8);

  ASSERT_EQ(parser.partWordBufferIndex, 0);
}

TEST_F(ChapterHtmlSlimParserTest, HeaderWithHiddenAttributeShouldBeSkipped) {
  const XML_Char* attributes[] = {"hidden", "hidden", nullptr};

  parser.beginParse();
  ChapterHtmlSlimParser::startElement(&parser, "h1", attributes);
  ChapterHtmlSlimParser::characterData(&parser, "[HIDDEN]", 8);

  ASSERT_EQ(parser.partWordBufferIndex, 0);
}

TEST_F(ChapterHtmlSlimParserTest, SpanWithHiddenAttributeShouldBeSkipped) {
  const XML_Char* attributes[] = {"hidden", "hidden", nullptr};

  parser.beginParse();
  ChapterHtmlSlimParser::startElement(&parser, "p", nullptr);
  ChapterHtmlSlimParser::characterData(&parser, "Before ", 7);
  ChapterHtmlSlimParser::startElement(&parser, "span", attributes);
  ChapterHtmlSlimParser::characterData(&parser, "[HIDDEN]", 8);
  ChapterHtmlSlimParser::endElement(&parser, "span");
  ChapterHtmlSlimParser::characterData(&parser, " After ", 7);

  ASSERT_EQ(parser.currentTextBlock->size(), 2);
  ASSERT_EQ(parser.currentTextBlock->wordAt(0), "Before");
  ASSERT_EQ(parser.currentTextBlock->wordAt(1), "After");
}

TEST_F(ChapterHtmlSlimParserTest, DivWithHiddenAttributeContentShouldBeSkipped) {
  const XML_Char* attributes[] = {"hidden", "hidden", nullptr};

  parser.beginParse();
  ChapterHtmlSlimParser::startElement(&parser, "div", attributes);
  ChapterHtmlSlimParser::startElement(&parser, "p", nullptr);
  ChapterHtmlSlimParser::characterData(&parser, "[HIDDEN]", 8);

  ASSERT_EQ(parser.partWordBufferIndex, 0);
}

TEST_F(ChapterHtmlSlimParserTest, PassesIndentSettingsToNewTextBlock) {
  for (bool extraSpacing : {false, true}) {
    parser.extraParagraphSpacing = extraSpacing;
    parser.currentTextBlock.reset();
    parser.setParagraphIndentSpaces(5);
    parser.startNewTextBlock(BlockStyle());
    ASSERT_NE(parser.currentTextBlock, nullptr);
    EXPECT_EQ(parser.currentTextBlock->paragraphIndentSpaces, 5);
  }
}

}  // namespace

TEST(ParagraphIndentation, OverridesNonnegativeCssAndPreservesHangingIndent) {
  GfxRenderer renderer;
  for (int cssIndent : {-6, 0, 13}) {
    for (uint8_t spaces : {0, 1, 2, 5}) {
      BlockStyle style;
      style.alignment = CssTextAlign::Left;
      style.textIndentDefined = true;
      style.textIndent = cssIndent;
      style.marginLeft = 6;  // room for the hanging indent
      ParsedText text(false, false, style, spaces);
      text.addWord("word", EpdFontFamily::REGULAR);
      bool sawLine = false;
      text.layoutAndExtractLines(renderer, 0, 200, [&](std::unique_ptr<TextBlock> line, auto) {
        sawLine = true;
        EXPECT_EQ(line->wordXpos(0), cssIndent < 0 ? cssIndent : 4 * spaces);
      });
      EXPECT_TRUE(sawLine);
    }
  }
  for (uint8_t spaces : {0, 2}) {
    BlockStyle style;
    style.alignment = CssTextAlign::Left;
    ParsedText text(false, false, style, spaces);
    text.addWord("word", EpdFontFamily::REGULAR);
    text.layoutAndExtractLines(
        renderer, 0, 200, [&](std::unique_ptr<TextBlock> line, auto) { EXPECT_EQ(line->wordXpos(0), 4 * spaces); });
  }
}

TEST(ParagraphIndentation, PreservesAlignmentEligibilityAndScaledSpaceRounding) {
  GfxRenderer renderer;
  for (const auto alignment : {CssTextAlign::Left, CssTextAlign::Center}) {
    for (uint8_t spaces : {0, 2}) {
      BlockStyle style;
      style.alignment = alignment;
      style.textIndentDefined = true;
      style.textIndent = 0;
      ParsedText text(false, false, style, spaces);
      text.addWord("word", EpdFontFamily::REGULAR);
      text.layoutAndExtractLines(renderer, 0, 200, [&](std::unique_ptr<TextBlock> line, auto) {
        if (alignment == CssTextAlign::Left)
          EXPECT_EQ(line->wordXpos(0), 4 * spaces);
        else
          EXPECT_EQ(line->wordXpos(0), 84);
      });
    }
  }
  BlockStyle style;
  style.alignment = CssTextAlign::Left;
  ParsedText text(false, false, style, 2);
  text.addWord("word", EpdFontFamily::REGULAR);
  text.layoutAndExtractLines(
      renderer, 0, 200, [&](std::unique_ptr<TextBlock> line, auto) { EXPECT_EQ(line->wordXpos(0), 6); }, true, 0, 75);
}

TEST(ParagraphIndentation, ReducesOnlyFirstLineAvailableWidth) {
  GfxRenderer renderer;
  BlockStyle style;
  style.alignment = CssTextAlign::Left;
  for (uint8_t spaces : {1, 2, 5}) {
    ParsedText text(false, false, style, spaces);
    text.addWord("ab", EpdFontFamily::REGULAR);
    text.addWord("cd", EpdFontFamily::REGULAR);
    unsigned lines = 0;
    text.layoutAndExtractLines(renderer, 0, 40, [&](std::unique_ptr<TextBlock>, auto) { ++lines; });
    EXPECT_EQ(lines, spaces == 1 ? 1u : 2u);
  }
}

TEST(ParagraphIndentation, PreviewKeyTracksOffAndWidths) {
  textsettings::PreviewKey off;
  EXPECT_EQ(off.paragraphIndentSpaces, 2);
  off.paragraphIndentSpaces = 0;
  auto on = off;
  on.paragraphIndentSpaces = 5;
  EXPECT_NE(off, on);
  on.paragraphIndentSpaces = 2;
  EXPECT_NE(off, on);
}

TEST(ParagraphIndentation, MigratesLegacySettingsAndClampsWidths) {
  EXPECT_EQ(migrateParagraphIndentSpaces(false, 0, true), 0);
  EXPECT_EQ(migrateParagraphIndentSpaces(false, 0, false), 2);
  EXPECT_EQ(migrateParagraphIndentSpaces(true, 0, false), 0);
  EXPECT_EQ(migrateParagraphIndentSpaces(true, 2, true), 2);
  EXPECT_EQ(migrateParagraphIndentSpaces(true, 5, false), 5);
  EXPECT_EQ(migrateParagraphIndentSpaces(true, -1, false), 0);
  EXPECT_EQ(migrateParagraphIndentSpaces(true, 300, false), 5);
}

TEST(TextSpacingLayout, TrackingSeparatesCjkTokensAndScalesWordSpaces) {
  GfxRenderer renderer;
  for (bool hyphenation : {false, true}) {
    BlockStyle style;
    style.alignment = CssTextAlign::Left;
    style.textIndentDefined = true;
    ParsedText text(hyphenation, false, style, 0);
    text.addWord("一二三", EpdFontFamily::REGULAR);
    text.addWord("四五", EpdFontFamily::REGULAR);
    unsigned lines = 0;
    text.layoutAndExtractLines(
        renderer, 0, 200,
        [&](std::unique_ptr<TextBlock> line, auto) {
          ++lines;
          ASSERT_EQ(line->wordCount(), 5);
          EXPECT_EQ(line->wordXpos(0), 0);
          EXPECT_EQ(line->wordXpos(1), 7);  // 8 px glyph, -1 px tracking
          EXPECT_EQ(line->wordXpos(2), 14);
          EXPECT_EQ(line->wordXpos(3), 28);  // 8 px glyph plus 150% of a 4 px space, no tracking
          EXPECT_EQ(line->wordXpos(4), 35);
        },
        true, -1, 150);
    EXPECT_EQ(lines, 1u);
  }
  EXPECT_EQ(renderer.getTextAdvanceX(0, "ab", EpdFontFamily::REGULAR), 16);
  EXPECT_EQ(renderer.getSpaceWidth(0, EpdFontFamily::REGULAR), 4);
}

TEST(TextSpacingLayout, WordSpacingChangesWrapThreshold) {
  GfxRenderer renderer;
  for (uint8_t percent : {50, 100, 125, 200}) {
    BlockStyle style;
    style.alignment = CssTextAlign::Left;
    style.textIndentDefined = true;
    ParsedText text(false, false, style, 0);
    text.addWord("ab", EpdFontFamily::REGULAR);
    text.addWord("cd", EpdFontFamily::REGULAR);
    unsigned lines = 0;
    text.layoutAndExtractLines(renderer, 0, 36, [&](std::unique_ptr<TextBlock>, auto) { ++lines; }, true, 0, percent);
    EXPECT_EQ(lines, percent > 100 ? 2u : 1u);  // 16 + 16 + scaled 4 px space
  }
}

TEST(TextSpacingLayout, CachedPageRestoresSpacing) {
  GfxRenderer renderer;
  BlockStyle style;
  style.alignment = CssTextAlign::Left;
  style.textIndentDefined = true;
  ParsedText text(false, false, style);
  text.addWord("一二三", EpdFontFamily::REGULAR);
  text.addWord("四五", EpdFontFamily::REGULAR);
  const auto path = (std::filesystem::temp_directory_path() / "crosspoint-text-spacing.bin").string();
  unsigned lines = 0;
  text.layoutAndExtractLines(
      renderer, 0, 200,
      [&](std::unique_ptr<TextBlock> line, auto) {
        ++lines;
        Page page;
        page.elements.push_back(std::make_unique<PageLine>(std::move(line), 4, 12));
        const auto* original = static_cast<const PageLine&>(*page.elements[0]).getBlock();
        {
          HalFile file;
          ASSERT_TRUE(file.open(path.c_str(), "wb"));
          ASSERT_TRUE(page.serialize(file));
        }
        HalFile file;
        ASSERT_TRUE(file.open(path.c_str(), "rb"));
        auto cachedPage = Page::deserialize(file);
        ASSERT_NE(cachedPage, nullptr);
        ASSERT_EQ(cachedPage->elements.size(), 1);
        const auto* cached = static_cast<const PageLine&>(*cachedPage->elements[0]).getBlock();
        ASSERT_NE(cached, nullptr);
        EXPECT_EQ(cached->getBlockStyle().characterSpacing, -2);
        ASSERT_EQ(cached->wordCount(), 5);
        EXPECT_EQ(cached->wordXpos(3) - cached->wordXpos(2), 10);  // 8 + half-width space
        EXPECT_EQ(file.position(), file.size());
        ASSERT_EQ(cached->wordCount(), original->wordCount());
        for (uint16_t i = 0; i < original->wordCount(); ++i) EXPECT_EQ(cached->wordXpos(i), original->wordXpos(i));
      },
      true, -2, 50);
  EXPECT_EQ(lines, 1u);
  std::filesystem::remove(path);
}

TEST_F(ChapterHtmlSlimParserTest, ParserAppliesTextSpacingToParagraphs) {
  parser.setTextSpacing(-1, 150);
  parser.beginParse();
  ChapterHtmlSlimParser::startElement(&parser, "p", nullptr);
  const std::string text = "\xe4\xb8\x80\xe4\xba\x8c\xe4\xb8\x89 \xe5\x9b\x9b\xe4\xba\x94";  // 一二三 四五
  ChapterHtmlSlimParser::characterData(&parser, text.c_str(), static_cast<int>(text.size()));
  ChapterHtmlSlimParser::endElement(&parser, "p");
  parser.makePages();
  ASSERT_NE(parser.currentPage, nullptr);
  unsigned lines = 0;
  for (const auto& element : parser.currentPage->elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto& block = *static_cast<const PageLine&>(*element).getBlock();
    ++lines;
    ASSERT_EQ(block.wordCount(), 5);
    EXPECT_EQ(block.getBlockStyle().characterSpacing, -1);
    EXPECT_EQ(block.wordXpos(1) - block.wordXpos(0), 7);   // 8 px glyph, -1 px tracking
    EXPECT_EQ(block.wordXpos(3) - block.wordXpos(2), 14);  // glyph plus 150% of a 4 px space
  }
  EXPECT_EQ(lines, 1u);
}

TEST(KoreanLayout, HangulWordsStayWholeAndWrapAtSpaces) {
  GfxRenderer renderer;
  {
    BlockStyle style;
    style.alignment = CssTextAlign::Left;
    style.textIndentDefined = true;
    ParsedText text(false, false, style, 0);
    text.addWord("가나다", EpdFontFamily::REGULAR);
    text.addWord("라마", EpdFontFamily::REGULAR);
    text.addWord("3개를", EpdFontFamily::REGULAR);
    text.addWord("iPhone을", EpdFontFamily::REGULAR);
    std::vector<std::vector<std::string>> lines;
    text.layoutAndExtractLines(renderer, 0, 60, [&](std::unique_ptr<TextBlock> line, auto) {
      auto& words = lines.emplace_back();
      for (uint16_t i = 0; i < line->wordCount(); ++i) words.emplace_back(line->wordText(i));
    });
    // 가나다 라마 is 24 + 4 + 16 px; adding 3개를 would need 72 px, and no break exists inside it.
    const std::vector<std::vector<std::string>> expected{{"가나다", "라마"}, {"3개를"}, {"iPhone을"}};
    EXPECT_EQ(lines, expected);
  }
}

TEST(KoreanLayout, JustifiedHangulStretchesOnlyWordSpaces) {
  GfxRenderer renderer;
  BlockStyle style;
  style.alignment = CssTextAlign::Justify;
  style.textIndentDefined = true;
  ParsedText text(false, false, style, 0);
  for (const char* word : {"가나", "다라", "마바", "사아"}) text.addWord(word, EpdFontFamily::REGULAR);
  unsigned lines = 0;
  text.layoutAndExtractLines(renderer, 0, 60, [&](std::unique_ptr<TextBlock> line, auto) {
    if (lines++ != 0) return;
    // 3 x 16 px words + 2 x 4 px spaces leave 4 px, split across the two spaces only.
    ASSERT_EQ(line->wordCount(), 3);
    EXPECT_EQ(line->wordXpos(0), 0);
    EXPECT_EQ(line->wordXpos(1), 22);
    EXPECT_EQ(line->wordXpos(2), 44);
  });
  EXPECT_EQ(lines, 2u);
}

TEST(KoreanLayout, HangulGluedAcrossInlineStyleIsUnbreakable) {
  GfxRenderer renderer;
  BlockStyle style;
  style.alignment = CssTextAlign::Justify;
  style.textIndentDefined = true;
  ParsedText text(false, false, style);
  text.addWord("가나", EpdFontFamily::REGULAR);
  text.addWord("한국", EpdFontFamily::REGULAR);
  text.addWord("어", EpdFontFamily::BOLD, false, /*attachToPrevious=*/true);
  std::vector<std::vector<std::string>> lines;
  text.layoutAndExtractLines(renderer, 0, 40, [&](std::unique_ptr<TextBlock> line, auto) {
    auto& words = lines.emplace_back();
    for (uint16_t i = 0; i < line->wordCount(); ++i) words.emplace_back(line->wordText(i));
  });
  // 가나 한국 fits in 36 px, but 어 is glued to 한국, so the whole word moves down.
  const std::vector<std::vector<std::string>> expected{{"가나"}, {"한국", "어"}};
  EXPECT_EQ(lines, expected);
}

TEST(ParagraphIndentation, HangingIndentStopsAtContentStartEdge) {
  GfxRenderer renderer;
  for (const int16_t margin : {0, 6, 30}) {
    BlockStyle style;
    style.alignment = CssTextAlign::Left;
    style.textIndentDefined = true;
    style.textIndent = -20;
    style.marginLeft = margin;
    ParsedText text(false, false, style, 2);
    text.addWord("word", EpdFontFamily::REGULAR);
    bool sawLine = false;
    text.layoutAndExtractLines(renderer, 0, 200, [&](std::unique_ptr<TextBlock> line, auto) {
      sawLine = true;
      EXPECT_EQ(line->wordXpos(0), -std::min<int>(margin, 20));
      EXPECT_GE(margin + line->wordXpos(0), 0);
    });
    EXPECT_TRUE(sawLine);
  }
}

// Book-wide stylesheet merging: a later sheet's negative text-indent for the same selector,
// combined with yet another sheet's smaller margin, must not push the line off the page.
TEST(ParagraphIndentation, MergedStylesheetsKeepHangingIndentOnPage) {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "crosspoint-merged-css-indent";
  fs::create_directories(dir);
  const auto html = (dir / "toc.xhtml").string();
  {
    HalFile out;
    ASSERT_TRUE(out.open(html.c_str(), "wb"));
    const char doc[] =
        "<html xmlns=\"http://www.w3.org/1999/xhtml\"><body><div class=\"toc\">"
        "<div class=\"toc_chap\">\n<a class=\"hlink\" href=\"c01.xhtml\">1: Helldiver</a>\n</div>"
        "<div class=\"toc_part0\">\n<a class=\"hlink\" href=\"p01.xhtml\">Part I: Slave</a>\n</div>"
        "</div></body></html>";
    out.write(doc, sizeof(doc) - 1);
  }
  const char* sheets[] = {
      "div.toc_chap {margin-left:1.6em; text-align:left; text-indent:0; font-size:0.9em}"
      "div.toc_part0 {text-align:left; margin-left:1.4em}",
      "div.toc_chap {text-align:left; text-indent:-1.7em; font-size:0.9em}",
      "div.toc_chap, div.toc_sub {margin-left:3%}",
  };
  CssParser cssParser{dir.string()};
  for (size_t i = 0; i < std::size(sheets); ++i) {
    const auto path = (dir / ("s" + std::to_string(i) + ".css")).string();
    {
      HalFile out;
      ASSERT_TRUE(out.open(path.c_str(), "wb"));
      out.write(sheets[i], strlen(sheets[i]));
    }
    HalFile in;
    ASSERT_TRUE(in.open(path.c_str(), "rb"));
    cssParser.loadFromStream(in);
  }

  GfxRenderer renderer;
  std::vector<std::pair<std::string, int>> starts;
  ChapterHtmlSlimParser parser{nullptr, html, renderer, 0, 1.0f, false, 0, 220, 300, false, false,
                               [&](std::unique_ptr<Page> page, auto, auto, auto) {
                                 for (const auto& el : page->elements) {
                                   if (el->getTag() != TAG_PageLine) continue;
                                   const auto& block = *static_cast<const PageLine&>(*el).getBlock();
                                   if (block.wordCount() == 0) continue;
                                   starts.emplace_back(block.wordText(0), el->xPos + block.wordXpos(0));
                                 }
                               },
                               true, "", "", 0, {}, nullptr, &cssParser};
  ASSERT_TRUE(parser.parseAndBuildPages());
  ASSERT_EQ(starts.size(), 2u);
  EXPECT_EQ(starts[0].first, "1:");
  EXPECT_EQ(starts[0].second, 0);  // 3% margin (6) + -1.7em indent (-20), clamped at the content edge
  EXPECT_EQ(starts[1].first, "Part");
  EXPECT_EQ(starts[1].second, 16 + 8);  // 1.4em margin + default 2-space paragraph indent
}

// Lays out `doc` (stored at OEBPS/xhtml/doc.xhtml) against stylesheets registered as a
// scoped rule set in manifest order, returning each line's first word and its x.
struct ScopedSheet {
  const char* path;
  const char* css;
};
std::vector<std::pair<std::string, int>> layoutWithScopedSheets(const char* testName, const std::string& doc,
                                                                 std::initializer_list<ScopedSheet> sheets) {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / testName;
  fs::create_directories(dir);
  const auto html = (dir / "doc.xhtml").string();
  {
    HalFile out;
    EXPECT_TRUE(out.open(html.c_str(), "wb"));
    out.write(doc.data(), doc.size());
  }
  CssParser cssParser{dir.string()};
  EXPECT_TRUE(cssParser.reserveStylesheets(sheets.size()));
  uint16_t sheetId = 0;
  for (const ScopedSheet& sheet : sheets) {
    const auto path = (dir / ("s" + std::to_string(sheetId) + ".css")).string();
    {
      HalFile out;
      EXPECT_TRUE(out.open(path.c_str(), "wb"));
      out.write(sheet.css, strlen(sheet.css));
    }
    HalFile in;
    EXPECT_TRUE(in.open(path.c_str(), "rb"));
    cssParser.addStylesheet(sheet.path, sheetId);
    cssParser.loadFromStream(in, sheetId, sheet.path);
    ++sheetId;
  }

  GfxRenderer renderer;
  std::vector<std::pair<std::string, int>> starts;
  ChapterHtmlSlimParser parser{nullptr, html, renderer, 0, 1.0f, false, 0, 220, 300, false, false,
                               [&](std::unique_ptr<Page> page, auto, auto, auto) {
                                 for (const auto& el : page->elements) {
                                   if (el->getTag() != TAG_PageLine) continue;
                                   const auto& block = *static_cast<const PageLine&>(*el).getBlock();
                                   if (block.wordCount() == 0) continue;
                                   starts.emplace_back(block.wordText(0), el->xPos + block.wordXpos(0));
                                 }
                               },
                               true, "OEBPS/xhtml/", "", 0, {}, nullptr, &cssParser};
  EXPECT_TRUE(parser.parseAndBuildPages());
  return starts;
}

std::string xhtmlWithLinks(const char* links, const char* body) {
  return std::string("<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>t</title>") + links +
         "</head><body>" + body + "</body></html>";
}

// The Red Rising omnibus TOC links only style1; style2 and style6 (linked by other
// documents) define the same selector with a hanging indent and a 3% margin.
TEST(StylesheetScoping, TocUsesOnlyItsLinkedStylesheet) {
  const std::string doc = xhtmlWithLinks(
      "<link href=\"../css/9780593725320_style1.css\" rel=\"stylesheet\" type=\"text/css\"/>",
      "<div class=\"toc\"><div class=\"toc_part0\">\n<a class=\"hlink\" href=\"p01.xhtml\">Part I: Slave</a>\n</div>"
      "<div class=\"toc_chap\">\n<a class=\"hlink\" href=\"c01.xhtml\">1: Helldiver</a>\n</div></div>");
  const auto starts = layoutWithScopedSheets(
      "crosspoint-scoped-css-toc", doc,
      {
          {"OEBPS/css/9780593725320_style.css", "div.toc_chap {margin-left:0; text-indent:-3em}"},
          {"OEBPS/css/9780593725320_style1.css",
           "div.toc_chap {margin-left:1.6em; text-align:left; text-indent:0; font-size:0.9em; line-height:1.4em}"
           "div.toc_part0 {margin-top:0; text-align:left; margin-left:1.4em; line-height:1.5em; font-size:1em}"
           "div.toc {margin-right:2em; text-align:justify}"},
          {"OEBPS/css/9780593725320_style2.css", "div.toc_chap {text-align:left; text-indent:-1.7em; font-size:0.9em}"},
          {"OEBPS/css/9780593725320_style6.css", "div.toc_chap, div.toc_sub {margin-left:3%}"},
      });
  ASSERT_EQ(starts.size(), 2u);
  EXPECT_EQ(starts[0].first, "Part");
  EXPECT_EQ(starts[1].first, "1:");
  // 1.6em margin from style1 alone: clearly inset, and right of the part heading.
  EXPECT_GT(starts[1].second, 0);
  EXPECT_GT(starts[1].second, starts[0].second);
  EXPECT_EQ(starts[1].second, 19 + 8);  // 1.6em margin + default 2-space paragraph indent
}

TEST(StylesheetScoping, UnlinkedSheetDoesNotApply) {
  const char* body = "<p class=\"x\">Alpha</p>";
  const std::initializer_list<ScopedSheet> sheets = {
      {"OEBPS/css/a.css", "p.x {margin-left:2em; text-indent:0}"},
      {"OEBPS/css/b.css", "p.x {margin-left:0; text-indent:0}"},
  };
  const auto linkedA = layoutWithScopedSheets(
      "crosspoint-scoped-css-a", xhtmlWithLinks("<link rel=\"stylesheet\" href=\"../css/a.css\"/>", body), sheets);
  ASSERT_EQ(linkedA.size(), 1u);
  const auto linkedB = layoutWithScopedSheets(
      "crosspoint-scoped-css-b", xhtmlWithLinks("<link rel=\"stylesheet\" href=\"../css/b.css\"/>", body), sheets);
  ASSERT_EQ(linkedB.size(), 1u);
  EXPECT_GT(linkedA[0].second, linkedB[0].second);

  // Linking both: the later link wins ties, so order matters.
  const auto ab = layoutWithScopedSheets(
      "crosspoint-scoped-css-ab",
      xhtmlWithLinks("<link rel=\"stylesheet\" href=\"../css/a.css\"/><link rel=\"stylesheet\" href=\"../css/b.css\"/>",
                     body),
      sheets);
  const auto ba = layoutWithScopedSheets(
      "crosspoint-scoped-css-ba",
      xhtmlWithLinks("<link rel=\"stylesheet\" href=\"../css/b.css\"/><link rel=\"stylesheet\" href=\"../css/a.css\"/>",
                     body),
      sheets);
  ASSERT_EQ(ab.size(), 1u);
  ASSERT_EQ(ba.size(), 1u);
  EXPECT_EQ(ab[0].second, linkedB[0].second);
  EXPECT_EQ(ba[0].second, linkedA[0].second);

  // No links: no external rules. Alternate sheets are not applied either.
  const auto none = layoutWithScopedSheets("crosspoint-scoped-css-none", xhtmlWithLinks("", body), sheets);
  const auto alternate = layoutWithScopedSheets(
      "crosspoint-scoped-css-alt",
      xhtmlWithLinks("<link rel=\"alternate stylesheet\" href=\"../css/a.css\"/>", body), sheets);
  ASSERT_EQ(none.size(), 1u);
  ASSERT_EQ(alternate.size(), 1u);
  EXPECT_EQ(none[0].second, alternate[0].second);
  EXPECT_LT(none[0].second, linkedA[0].second);

  // Links that resolve to no known sheet fall back to every sheet in manifest order (b wins).
  const auto unresolved = layoutWithScopedSheets(
      "crosspoint-scoped-css-unresolved",
      xhtmlWithLinks("<link rel=\"stylesheet\" href=\"../styles/missing.css\"/>", body), sheets);
  ASSERT_EQ(unresolved.size(), 1u);
  EXPECT_EQ(unresolved[0].second, linkedB[0].second);
}

TEST(BlockStyleInsetCap, ScalesPositiveInsetsProportionally) {
  BlockStyle style;
  style.marginLeft = 60;
  style.paddingLeft = 20;
  style.marginRight = 40;
  style.paddingRight = -5;

  const BlockStyle capped = style.withHorizontalInsetCap(60);
  EXPECT_EQ(capped.marginLeft, 30);
  EXPECT_EQ(capped.paddingLeft, 10);
  EXPECT_EQ(capped.marginRight, 20);
  EXPECT_EQ(capped.paddingRight, -5);

  const BlockStyle untouched = style.withHorizontalInsetCap(200);
  EXPECT_EQ(untouched.marginLeft, 60);
  EXPECT_EQ(untouched.marginRight, 40);
}
