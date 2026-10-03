#pragma once

#include <HalStorage.h>

#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "CssStyle.h"

/**
 * Lightweight CSS parser for EPUB stylesheets
 *
 * Parses CSS files and extracts styling information relevant for e-ink display.
 * Uses a two-phase approach: first tokenizes the CSS content, then builds
 * a rule database that can be queried during HTML parsing.
 *
 * Supported selectors:
 *   - Element selectors: p, div, h1, etc.
 *   - Class selectors: .classname
 *   - Combined: element.classname
 *   - Grouped: selector1, selector2 { }
 *
 * Not supported (silently ignored):
 *   - Descendant/child selectors
 *   - Pseudo-classes and pseudo-elements
 *   - Media queries (content is skipped)
 *   - @font-face, etc.
 *
 * DECKPOINT: rules are keyed by (selector, stylesheet). Each document is styled
 * only by the sheets it links (plus their @imports), in link order; see
 * StylesheetScope. A parser without a stylesheet table is unscoped and every
 * rule applies everywhere.
 */
class CssParser {
 public:
  enum class ParseResult : uint8_t {
    Complete,
    Partial,
    Error,
  };

  enum class CacheStatus : uint8_t {
    Missing,
    Complete,
    Partial,
    Invalid,
  };

  enum class CacheLoadResult : uint8_t {
    Complete,
    LowMemory,
    Invalid,
  };

  // Bump when CSS cache format or rules change; section caches are invalidated when this changes
  // DECKPOINT: v13 adds per-rule stylesheet ids, the stylesheet path table and @import edges.
  static constexpr uint8_t CSS_CACHE_VERSION = 13;

  // DECKPOINT: stylesheet scoping.
  static constexpr uint16_t MAX_STYLESHEETS = 512;
  static constexpr uint8_t MAX_SCOPE_SHEETS = 16;
  static constexpr uint8_t MAX_IMPORTS = 32;

  // Ordered stylesheets applying to one document. `all` applies every rule in
  // manifest order (the unscoped behaviour).
  struct StylesheetScope {
    uint16_t sheets[MAX_SCOPE_SHEETS] = {};
    uint8_t count = 0;
    bool all = true;
  };

  // FNV-1a over a normalised archive path; identifies a stylesheet without storing its path.
  static constexpr uint32_t hashStylesheetPath(std::string_view path) {
    uint32_t hash = 2166136261u;
    for (const char c : path) {
      hash ^= static_cast<uint8_t>(c);
      hash *= 16777619u;
    }
    return hash;
  }

  explicit CssParser(std::string cachePath) : cachePath(std::move(cachePath)) {}
  ~CssParser() = default;

  // Non-copyable
  CssParser(const CssParser&) = delete;
  CssParser& operator=(const CssParser&) = delete;

  /**
   * Load and parse CSS from a file stream.
   * Can be called multiple times to accumulate rules from multiple stylesheets.
   * @param source Open file handle to read from
   * @param sheet Stylesheet id the rules belong to (see addStylesheet)
   * @param sheetPath Normalised archive path of the sheet, used to resolve @import
   * @return Complete unless bounded storage stopped rule growth or the source was invalid
   */
  ParseResult loadFromStream(HalFile& source, uint16_t sheet = 0, std::string_view sheetPath = {});

  /**
   * DECKPOINT: allocate the stylesheet path table. Without a table the parser is
   * unscoped. Returns false (leaving it unscoped) when count exceeds
   * MAX_STYLESHEETS or allocation fails.
   */
  bool reserveStylesheets(size_t count);
  /** Map a normalised archive path to a stylesheet id (byte-identical sheets may share an id). */
  void addStylesheet(std::string_view path, uint16_t sheet);
  [[nodiscard]] bool isScoped() const { return sheetCapacity_ > 0; }

  /**
   * Append a linked stylesheet (and, first, the sheets it @imports) to scope.
   * @return false when the path is not a known stylesheet
   */
  bool appendToScope(std::string_view normalisedPath, StylesheetScope& scope) const;

  /**
   * Look up the style for an HTML element, considering tag name and class attributes.
   * Applies CSS cascade: element style < class style < element.class style
   *
   * @param tagName The HTML element name (e.g., "p", "div")
   * @param classAttr The class attribute value (may contain multiple space-separated classes)
   * @return Combined style with all applicable rules merged
   */
  [[nodiscard]] CssStyle resolveStyle(std::string_view tagName, std::string_view classAttr) const {
    return resolveStyle(tagName, classAttr, StylesheetScope{});
  }
  /** Resolve using only the scope's sheets; for equal specificity, later sheets win. */
  [[nodiscard]] CssStyle resolveStyle(std::string_view tagName, std::string_view classAttr,
                                      const StylesheetScope& scope) const;

  /**
   * Parse an inline style attribute string.
   * @param styleValue The value of a style="" attribute
   * @return Parsed style properties
   */
  [[nodiscard]] static CssStyle parseInlineStyle(std::string_view styleValue);

  /**
   * Check if any rules have been loaded
   */
  [[nodiscard]] bool empty() const { return entryCount_ == 0; }

  /**
   * Get count of loaded rule sets
   */
  [[nodiscard]] size_t ruleCount() const { return entryCount_; }

  /**
   * Clear all loaded rules
   */
  void clear() {
    entries_.reset();
    selectorPool_.reset();
    stylePool_.reset();
    entryCount_ = entryCapacity_ = 0;
    selectorPoolSize_ = selectorPoolCapacity_ = 0;
    styleCount_ = styleCapacity_ = 0;
    ruleGrowthStopped_ = false;
    sheetTable_.reset();
    imports_.reset();
    sheetCount_ = sheetCapacity_ = 0;
    importCount_ = 0;
    currentSheet_ = 0;
  }

  /**
   * Check if CSS rules cache file exists
   */
  bool hasCache() const;

  /** Read the cache header without hydrating its rule map. */
  CacheStatus inspectCache() const;

  /**
   * Delete CSS rules cache file exists
   */
  void deleteCache() const;

  /**
   * Save parsed CSS rules to a cache file.
   * @return true if cache was written successfully
   */
  bool saveToCache(bool complete) const;

  /**
   * Load CSS rules from a cache file.
   * Clears any existing rules before loading.
   * @return Complete when loaded, LowMemory when it should be retried, otherwise Invalid
   */
  CacheLoadResult loadFromCache();

 private:
  enum class RuleInsertResult : uint8_t {
    Inserted,
    Merged,
    Limit,
    OutOfMemory,
  };

  enum class PoolResult : uint8_t {
    Ready,
    Limit,
    OutOfMemory,
  };

  // DECKPOINT: offset narrowed to 16 bits (the selector pool is capped at 32KB)
  // to make room for the stylesheet id without growing the entry. Entries are
  // sorted by (selector, sheet); entries sharing a selector share its text.
  struct SelectorEntry {
    uint16_t offset;
    uint16_t styleIndex;
    uint16_t length;
    uint16_t sheet;
  };
  static_assert(sizeof(SelectorEntry) == 8);

  struct SheetPathEntry {
    uint32_t pathHash;
    uint16_t sheet;
  };
  struct ImportEntry {
    uint32_t targetHash;
    uint16_t sheet;
  };

  // Bounded flat storage keeps every growth operation fallible and avoids the
  // throwing node allocations used by std::unordered_map.
  std::unique_ptr<SelectorEntry[]> entries_;
  std::unique_ptr<char[]> selectorPool_;
  std::unique_ptr<CssStyle[]> stylePool_;
  uint16_t entryCount_ = 0;
  uint16_t entryCapacity_ = 0;
  uint32_t selectorPoolSize_ = 0;
  uint32_t selectorPoolCapacity_ = 0;
  uint16_t styleCount_ = 0;
  uint16_t styleCapacity_ = 0;
  bool ruleGrowthStopped_ = false;

  std::unique_ptr<SheetPathEntry[]> sheetTable_;
  std::unique_ptr<ImportEntry[]> imports_;
  uint16_t sheetCount_ = 0;
  uint16_t sheetCapacity_ = 0;
  uint8_t importCount_ = 0;
  uint16_t currentSheet_ = 0;

  std::string cachePath;

  // Internal parsing helpers
  bool restoreCacheBackupIfNeeded() const;
  void processRuleBlockWithStyle(std::string_view selectorGroup, const CssStyle& style);
  void recordImport(std::string_view prelude, std::string_view sheetPath);
  bool appendToScope(uint32_t pathHash, StylesheetScope& scope, uint8_t depth) const;
  [[nodiscard]] int compareEntryToPieces(const SelectorEntry& entry, std::string_view p0, std::string_view p1,
                                         std::string_view p2) const;
  [[nodiscard]] size_t lowerBound(std::string_view p0, std::string_view p1, std::string_view p2, uint16_t sheet,
                                  bool& exact) const;
  [[nodiscard]] const CssStyle* findStyle(uint16_t sheet, std::string_view p0, std::string_view p1 = {},
                                          std::string_view p2 = {}) const;
  // Applies every sheet's rule for the selector, in sheet order (unscoped cascade).
  void applyAllSheets(CssStyle& result, std::string_view p0, std::string_view p1 = {},
                      std::string_view p2 = {}) const;
  [[nodiscard]] std::string_view selectorAt(size_t index) const;
  RuleInsertResult insertOrMerge(std::string_view selector, uint16_t sheet, const CssStyle& style);
  PoolResult ensureEntryCapacity(size_t needed);
  PoolResult ensureSelectorPoolCapacity(size_t needed);
  PoolResult ensureStyleCapacity(size_t needed);
  PoolResult internStyle(const CssStyle& style, uint16_t& indexOut);
  static CssStyle parseDeclarations(std::string_view declBlock);
  static void parseDeclarationIntoStyle(std::string_view decl, CssStyle& style);

  // Individual property value parsers
  static CssTextAlign interpretAlignment(std::string_view val);
  static CssFontStyle interpretFontStyle(std::string_view val);
  static CssFontWeight interpretFontWeight(std::string_view val);
  static CssTextDecoration interpretDecoration(std::string_view val);
  static CssLength interpretLength(std::string_view val);
  /** Returns true only when a numeric length was parsed (e.g. 2em, 50%). False for auto/inherit/initial. */
  static bool tryInterpretLength(std::string_view val, CssLength& out);
};
