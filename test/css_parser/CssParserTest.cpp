#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

#include "CssParser.h"

namespace fs = std::filesystem;

namespace {

constexpr size_t kMaxRules = 1500;
constexpr size_t kMaxUniqueStyles = 256;
// version, flags, rule count, stylesheet count, import count (unscoped: no table records).
constexpr size_t kCacheHeaderBytes = sizeof(uint8_t) * 2 + sizeof(uint16_t) * 2 + sizeof(uint8_t);
constexpr size_t kStyleEnumPrefixBytes = 5;
constexpr size_t kStyleLengthFieldCount = 11;
constexpr size_t kStyleLengthBytes = sizeof(decltype(CssLength::value)) + sizeof(uint8_t);

class CssParserTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    directory_ = fs::temp_directory_path() / "crosspoint_css_parser_test" / info->name();
    fs::remove_all(directory_);
    fs::create_directories(directory_);
    Storage.clearFailures();
  }

  void TearDown() override {
    Storage.clearFailures();
    fs::remove_all(directory_);
  }

  std::string cachePath() const { return directory_.string(); }
  fs::path cacheFile() const { return directory_ / "css_rules.cache"; }
  fs::path cacheTempFile() const { return directory_ / "css_rules.cache.tmp"; }
  fs::path cacheBackupFile() const { return directory_ / "css_rules.cache.bak"; }

  std::vector<uint8_t> readCache() const {
    std::ifstream input(cacheFile(), std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  }

  void writeCache(const std::vector<uint8_t>& bytes) const {
    std::ofstream output(cacheFile(), std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }

  CssParser::ParseResult loadCss(CssParser& parser, const std::string& css) const {
    const fs::path sourcePath = directory_ / "input.css";
    std::ofstream output(sourcePath, std::ios::binary);
    output.write(css.data(), static_cast<std::streamsize>(css.size()));
    output.close();

    HalFile source;
    EXPECT_TRUE(HalStorage::getInstance().openFileForRead("TST", sourcePath.string(), source));
    return parser.loadFromStream(source);
  }

  CssParser::ParseResult loadSheet(CssParser& parser, const std::string& css, const uint16_t sheet,
                                   const std::string& sheetPath) const {
    const fs::path sourcePath = directory_ / "input.css";
    std::ofstream output(sourcePath, std::ios::binary | std::ios::trunc);
    output.write(css.data(), static_cast<std::streamsize>(css.size()));
    output.close();

    HalFile source;
    EXPECT_TRUE(HalStorage::getInstance().openFileForRead("TST", sourcePath.string(), source));
    parser.addStylesheet(sheetPath, sheet);
    return parser.loadFromStream(source, sheet, sheetPath);
  }

  static CssParser::StylesheetScope scopeOf(const CssParser& parser, std::initializer_list<const char*> links) {
    CssParser::StylesheetScope scope;
    scope.all = false;
    for (const char* link : links) EXPECT_TRUE(parser.appendToScope(link, scope)) << link;
    return scope;
  }

  fs::path directory_;
};

TEST_F(CssParserTest, ResolvesCaseInsensitiveCascadeAndMergesDuplicates) {
  CssParser parser(cachePath());
  ASSERT_EQ(loadCss(parser,
                    "P { text-align: center; }\n"
                    ".Note { font-weight: bold; text-align: right; }\n"
                    "p.note { font-style: italic; text-align: justify; }\n"
                    ".note { margin-top: 2em; }\n"),
            CssParser::ParseResult::Complete);

  EXPECT_EQ(parser.ruleCount(), 3u);
  const CssStyle style = parser.resolveStyle("p", "NOTE");
  EXPECT_EQ(style.textAlign, CssTextAlign::Justify);
  EXPECT_EQ(style.fontWeight, CssFontWeight::Bold);
  EXPECT_EQ(style.fontStyle, CssFontStyle::Italic);
  ASSERT_TRUE(style.hasMarginTop());
  EXPECT_FLOAT_EQ(style.marginTop.value, 2.0f);
  EXPECT_EQ(style.marginTop.unit, CssUnit::Em);
}

TEST_F(CssParserTest, DeduplicatedStylesReachTheBoundedRuleCap) {
  CssParser parser(cachePath());
  std::string css;
  for (size_t i = 0; i < kMaxRules + 20; ++i) {
    css += ".class-" + std::to_string(i) + " { font-weight: bold; text-indent: 1.5em; }\n";
  }

  EXPECT_EQ(loadCss(parser, css), CssParser::ParseResult::Partial);
  EXPECT_EQ(parser.ruleCount(), kMaxRules);
  EXPECT_EQ(parser.resolveStyle("div", "class-1499").fontWeight, CssFontWeight::Bold);
  EXPECT_FALSE(parser.resolveStyle("div", "class-1500").hasFontWeight());
}

TEST_F(CssParserTest, CascadeUpdatesContinueAfterRuleCap) {
  CssParser parser(cachePath());
  std::string css;
  for (size_t i = 0; i < kMaxRules + 20; ++i) {
    css += ".class-" + std::to_string(i) + " { font-weight: bold; }\n";
  }
  css += ".class-1499 { font-style: italic; }\n";

  EXPECT_EQ(loadCss(parser, css), CssParser::ParseResult::Partial);
  EXPECT_EQ(parser.ruleCount(), kMaxRules);
  const CssStyle style = parser.resolveStyle("div", "class-1499");
  EXPECT_EQ(style.fontWeight, CssFontWeight::Bold);
  EXPECT_EQ(style.fontStyle, CssFontStyle::Italic);
}

TEST_F(CssParserTest, UniqueStyleCapStopsWithoutCorruptingAcceptedRules) {
  CssParser parser(cachePath());
  std::string css;
  for (size_t i = 0; i < kMaxUniqueStyles + 20; ++i) {
    css += ".unique-" + std::to_string(i) + " { text-indent: " + std::to_string(i + 1) + "px; }\n";
  }

  EXPECT_EQ(loadCss(parser, css), CssParser::ParseResult::Partial);
  EXPECT_EQ(parser.ruleCount(), kMaxUniqueStyles);
  EXPECT_FLOAT_EQ(parser.resolveStyle("p", "unique-255").textIndent.value, 256.0f);
  EXPECT_FALSE(parser.resolveStyle("p", "unique-256").hasTextIndent());
}

TEST_F(CssParserTest, RepeatedOverridesReuseAnUnsharedStyleSlot) {
  CssParser parser(cachePath());
  std::string css;
  for (size_t i = 0; i < kMaxUniqueStyles + 20; ++i) {
    css += ".same { text-indent: " + std::to_string(i + 1) + "px; }\n";
  }

  EXPECT_EQ(loadCss(parser, css), CssParser::ParseResult::Complete);
  EXPECT_EQ(parser.ruleCount(), 1u);
  EXPECT_FLOAT_EQ(parser.resolveStyle("p", "same").textIndent.value, static_cast<float>(kMaxUniqueStyles + 20));
}

TEST_F(CssParserTest, OversizedSelectorGroupMarksParsePartialAndSkipsRule) {
  CssParser parser(cachePath());
  const std::string css = "." + std::string(1100, 'a') +
                          " { font-weight: bold; }\n"
                          ".valid { font-style: italic; }\n";

  EXPECT_EQ(loadCss(parser, css), CssParser::ParseResult::Partial);
  EXPECT_EQ(parser.ruleCount(), 1u);
  EXPECT_EQ(parser.resolveStyle("span", "valid").fontStyle, CssFontStyle::Italic);
}

TEST_F(CssParserTest, OversizedDeclarationMarksParsePartialAndKeepsFollowingDeclarations) {
  CssParser parser(cachePath());
  const std::string css = ".long { font-weight: " + std::string(1100, 'x') + "; font-style: italic; }\n";

  EXPECT_EQ(loadCss(parser, css), CssParser::ParseResult::Partial);
  EXPECT_EQ(parser.ruleCount(), 1u);
  const CssStyle style = parser.resolveStyle("span", "long");
  EXPECT_FALSE(style.hasFontWeight());
  EXPECT_EQ(style.fontStyle, CssFontStyle::Italic);
}

TEST_F(CssParserTest, IncompleteInputMarksParsePartial) {
  for (const char* css : {".a { font-weight: bold;", "@media screen {", "/* unfinished", ".unfinished"}) {
    CssParser parser(cachePath());
    EXPECT_EQ(loadCss(parser, css), CssParser::ParseResult::Partial) << css;
  }
}

TEST_F(CssParserTest, CanonicalCacheRoundTripPreservesStyles) {
  CssParser writer(cachePath());
  ASSERT_EQ(loadCss(writer,
                    "p { text-align: justify; margin-top: 2em; }\n"
                    ".bold { font-weight: bolder; }\n"
                    ".hidden { display: none; }\n"),
            CssParser::ParseResult::Complete);
  ASSERT_TRUE(writer.saveToCache(true));
  EXPECT_EQ(writer.inspectCache(), CssParser::CacheStatus::Complete);

  CssParser reader(cachePath());
  ASSERT_EQ(reader.loadFromCache(), CssParser::CacheLoadResult::Complete);
  EXPECT_EQ(reader.ruleCount(), writer.ruleCount());
  EXPECT_EQ(reader.resolveStyle("span", "bold").fontWeight, CssFontWeight::Bold);
  EXPECT_EQ(reader.resolveStyle("div", "hidden").display, CssDisplay::None);
  const CssStyle paragraph = reader.resolveStyle("p", "");
  EXPECT_EQ(paragraph.textAlign, CssTextAlign::Justify);
  EXPECT_FLOAT_EQ(paragraph.marginTop.value, 2.0f);
}

TEST_F(CssParserTest, PartialCacheIsValidatedDuringInspection) {
  CssParser writer(cachePath());
  ASSERT_EQ(loadCss(writer, ".a { font-weight: bold; }\n"), CssParser::ParseResult::Complete);
  ASSERT_TRUE(writer.saveToCache(false));
  ASSERT_EQ(writer.inspectCache(), CssParser::CacheStatus::Partial);

  const auto size = fs::file_size(cacheFile());
  fs::resize_file(cacheFile(), size - 1);
  EXPECT_EQ(writer.inspectCache(), CssParser::CacheStatus::Invalid);
}

TEST_F(CssParserTest, CompleteCacheDefersPayloadValidationToHydration) {
  CssParser writer(cachePath());
  ASSERT_EQ(loadCss(writer, ".a { font-weight: bold; }\n"), CssParser::ParseResult::Complete);
  ASSERT_TRUE(writer.saveToCache(true));

  const auto size = fs::file_size(cacheFile());
  fs::resize_file(cacheFile(), size - 1);
  EXPECT_EQ(writer.inspectCache(), CssParser::CacheStatus::Complete);

  CssParser reader(cachePath());
  EXPECT_EQ(reader.loadFromCache(), CssParser::CacheLoadResult::Invalid);
  EXPECT_TRUE(reader.empty());
}

TEST_F(CssParserTest, FailedPromotionRestoresTheOnlyBackupCache) {
  CssParser writer(cachePath());
  ASSERT_EQ(loadCss(writer, ".original { font-weight: bold; }\n"), CssParser::ParseResult::Complete);
  ASSERT_TRUE(writer.saveToCache(true));
  fs::rename(cacheFile(), cacheBackupFile());
  ASSERT_FALSE(fs::exists(cacheFile()));
  ASSERT_EQ(loadCss(writer, ".replacement { font-style: italic; }\n"), CssParser::ParseResult::Complete);

  Storage.failNextRename(cacheTempFile().string(), cacheFile().string());
  EXPECT_FALSE(writer.saveToCache(true));
  EXPECT_TRUE(fs::exists(cacheFile()));
  EXPECT_FALSE(fs::exists(cacheTempFile()));
  EXPECT_FALSE(fs::exists(cacheBackupFile()));

  CssParser reader(cachePath());
  ASSERT_EQ(reader.loadFromCache(), CssParser::CacheLoadResult::Complete);
  EXPECT_EQ(reader.ruleCount(), 1u);
  EXPECT_EQ(reader.resolveStyle("span", "original").fontWeight, CssFontWeight::Bold);
  EXPECT_FALSE(reader.resolveStyle("span", "replacement").hasFontStyle());
}

TEST_F(CssParserTest, CacheHydrationRejectsInvalidStyleEnumBytes) {
  CssParser writer(cachePath());
  ASSERT_EQ(loadCss(writer, ".a { font-weight: bold; margin-top: 2em; }\n"), CssParser::ParseResult::Complete);
  ASSERT_TRUE(writer.saveToCache(true));

  const std::vector<uint8_t> validCache = readCache();
  ASSERT_GE(validCache.size(), kCacheHeaderBytes + sizeof(uint16_t));
  uint16_t selectorLength = 0;
  memcpy(&selectorLength, validCache.data() + kCacheHeaderBytes, sizeof(selectorLength));
  const size_t styleOffset = kCacheHeaderBytes + sizeof(selectorLength) + selectorLength + sizeof(uint16_t);

  std::vector<size_t> enumOffsets = {0, 1, 2, 3, 4};
  for (size_t i = 0; i < kStyleLengthFieldCount; ++i) {
    enumOffsets.push_back(kStyleEnumPrefixBytes + i * kStyleLengthBytes + sizeof(decltype(CssLength::value)));
  }
  enumOffsets.push_back(kStyleEnumPrefixBytes + kStyleLengthFieldCount * kStyleLengthBytes);
  enumOffsets.push_back(kStyleEnumPrefixBytes + kStyleLengthFieldCount * kStyleLengthBytes + 1);

  for (const size_t enumOffset : enumOffsets) {
    SCOPED_TRACE(enumOffset);
    std::vector<uint8_t> corruptedCache = validCache;
    ASSERT_LT(styleOffset + enumOffset, corruptedCache.size());
    corruptedCache[styleOffset + enumOffset] = 0xff;
    writeCache(corruptedCache);

    CssParser reader(cachePath());
    EXPECT_EQ(reader.loadFromCache(), CssParser::CacheLoadResult::Invalid);
    EXPECT_TRUE(reader.empty());
  }
}

TEST_F(CssParserTest, CacheHydrationRejectsNonFiniteStyleLengths) {
  CssParser writer(cachePath());
  ASSERT_EQ(loadCss(writer, ".a { margin-top: 2em; }\n"), CssParser::ParseResult::Complete);
  ASSERT_TRUE(writer.saveToCache(true));

  const std::vector<uint8_t> validCache = readCache();
  ASSERT_GE(validCache.size(), kCacheHeaderBytes + sizeof(uint16_t));
  uint16_t selectorLength = 0;
  memcpy(&selectorLength, validCache.data() + kCacheHeaderBytes, sizeof(selectorLength));
  const size_t firstLengthOffset =
      kCacheHeaderBytes + sizeof(selectorLength) + selectorLength + sizeof(uint16_t) + kStyleEnumPrefixBytes;

  using LengthValue = decltype(CssLength::value);
  for (const LengthValue invalidValue :
       {std::numeric_limits<LengthValue>::quiet_NaN(), std::numeric_limits<LengthValue>::infinity(),
        -std::numeric_limits<LengthValue>::infinity()}) {
    std::vector<uint8_t> corruptedCache = validCache;
    ASSERT_LE(firstLengthOffset + sizeof(invalidValue), corruptedCache.size());
    memcpy(corruptedCache.data() + firstLengthOffset, &invalidValue, sizeof(invalidValue));
    writeCache(corruptedCache);

    CssParser reader(cachePath());
    EXPECT_EQ(reader.loadFromCache(), CssParser::CacheLoadResult::Invalid);
    EXPECT_TRUE(reader.empty());
  }
}

TEST_F(CssParserTest, ScopedResolutionIgnoresUnlinkedSheets) {
  CssParser parser(cachePath());
  ASSERT_TRUE(parser.reserveStylesheets(2));
  ASSERT_EQ(loadSheet(parser, "div.toc_chap { margin-left: 1.6em; text-indent: 0; }", 0, "OEBPS/css/style1.css"),
            CssParser::ParseResult::Complete);
  ASSERT_EQ(loadSheet(parser, "div.toc_chap { text-indent: -1.7em; margin-left: 3%; }", 1, "OEBPS/css/style2.css"),
            CssParser::ParseResult::Complete);
  EXPECT_EQ(parser.ruleCount(), 2u);

  const CssStyle linked = parser.resolveStyle("div", "toc_chap", scopeOf(parser, {"OEBPS/css/style1.css"}));
  EXPECT_FLOAT_EQ(linked.marginLeft.value, 1.6f);
  EXPECT_EQ(linked.marginLeft.unit, CssUnit::Em);
  EXPECT_FLOAT_EQ(linked.textIndent.value, 0.0f);

  CssParser::StylesheetScope none;
  none.all = false;
  EXPECT_FALSE(parser.resolveStyle("div", "toc_chap", none).hasMarginLeft());

  // Unscoped resolution keeps the book-wide cascade in manifest order.
  const CssStyle merged = parser.resolveStyle("div", "toc_chap");
  EXPECT_FLOAT_EQ(merged.textIndent.value, -1.7f);
  EXPECT_EQ(merged.marginLeft.unit, CssUnit::Percent);
}

TEST_F(CssParserTest, LinkOrderDecidesEqualSpecificityCascade) {
  CssParser parser(cachePath());
  ASSERT_TRUE(parser.reserveStylesheets(2));
  loadSheet(parser, "p { text-align: center; } .a { font-weight: bold; }", 0, "a.css");
  loadSheet(parser, "p { text-align: right; } .b { font-weight: normal; }", 1, "b.css");

  EXPECT_EQ(parser.resolveStyle("p", "", scopeOf(parser, {"a.css", "b.css"})).textAlign, CssTextAlign::Right);
  EXPECT_EQ(parser.resolveStyle("p", "", scopeOf(parser, {"b.css", "a.css"})).textAlign, CssTextAlign::Center);
  // Different selectors of equal specificity follow sheet order, not class-attribute order.
  EXPECT_EQ(parser.resolveStyle("p", "b a", scopeOf(parser, {"a.css", "b.css"})).fontWeight, CssFontWeight::Normal);
  EXPECT_EQ(parser.resolveStyle("p", "b a", scopeOf(parser, {"b.css", "a.css"})).fontWeight, CssFontWeight::Bold);
}

TEST_F(CssParserTest, ImportedSheetPrecedesImportingSheet) {
  CssParser parser(cachePath());
  ASSERT_TRUE(parser.reserveStylesheets(3));
  loadSheet(parser, "@import url(\"../shared/base.css\");\n p { text-align: right; }", 0, "OEBPS/css/main.css");
  loadSheet(parser, "p { text-align: center; margin-top: 1em; }", 1, "OEBPS/shared/base.css");
  loadSheet(parser, "@import 'print.css' print; p { font-style: italic; }", 2, "OEBPS/css/other.css");

  CssParser::StylesheetScope scope = scopeOf(parser, {"OEBPS/css/main.css"});
  ASSERT_EQ(scope.count, 2u);
  EXPECT_EQ(scope.sheets[0], 1u);
  EXPECT_EQ(scope.sheets[1], 0u);
  const CssStyle style = parser.resolveStyle("p", "", scope);
  EXPECT_EQ(style.textAlign, CssTextAlign::Right);
  EXPECT_FLOAT_EQ(style.marginTop.value, 1.0f);

  // Media-qualified imports are ignored.
  EXPECT_EQ(scopeOf(parser, {"OEBPS/css/other.css"}).count, 1u);
  CssParser::StylesheetScope unknown;
  EXPECT_FALSE(parser.appendToScope("OEBPS/css/missing.css", unknown));
}

TEST_F(CssParserTest, ScopedCacheRoundTripPreservesSheetsAndImports) {
  CssParser writer(cachePath());
  ASSERT_TRUE(writer.reserveStylesheets(3));
  loadSheet(writer, "@import \"b.css\"; .x { margin-left: 2em; }", 0, "a.css");
  loadSheet(writer, ".x { text-indent: 1em; margin-left: 5%; }", 1, "b.css");
  writer.addStylesheet("a-copy.css", 0);  // byte-identical duplicate shares sheet 0
  ASSERT_TRUE(writer.saveToCache(true));
  EXPECT_EQ(writer.inspectCache(), CssParser::CacheStatus::Complete);
  ASSERT_TRUE(writer.saveToCache(false));
  EXPECT_EQ(writer.inspectCache(), CssParser::CacheStatus::Partial);
  ASSERT_TRUE(writer.saveToCache(true));

  CssParser reader(cachePath());
  ASSERT_EQ(reader.loadFromCache(), CssParser::CacheLoadResult::Complete);
  EXPECT_TRUE(reader.isScoped());
  EXPECT_EQ(reader.ruleCount(), 2u);
  const CssStyle onlyB = reader.resolveStyle("div", "x", scopeOf(reader, {"b.css"}));
  EXPECT_EQ(onlyB.marginLeft.unit, CssUnit::Percent);
  const CssStyle viaCopy = reader.resolveStyle("div", "x", scopeOf(reader, {"a-copy.css"}));
  EXPECT_FLOAT_EQ(viaCopy.marginLeft.value, 2.0f);
  EXPECT_EQ(viaCopy.marginLeft.unit, CssUnit::Em);
  EXPECT_FLOAT_EQ(viaCopy.textIndent.value, 1.0f);  // from the imported b.css
}

TEST_F(CssParserTest, TooManyStylesheetsLeavesParserUnscoped) {
  CssParser parser(cachePath());
  EXPECT_FALSE(parser.reserveStylesheets(CssParser::MAX_STYLESHEETS + 1));
  EXPECT_FALSE(parser.isScoped());
  EXPECT_TRUE(parser.reserveStylesheets(CssParser::MAX_STYLESHEETS));
}

}  // namespace
