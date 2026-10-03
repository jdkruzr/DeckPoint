#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "deckpoint/reader/SearchMatch.h"

using deckpoint::reader::endsWithHyphenBreak;
using deckpoint::reader::foldCodepoint;
using deckpoint::reader::PhraseMatcher;
using deckpoint::reader::SearchQuery;

namespace {

std::string foldAll(const char* text) {
  SearchQuery q;
  q.set(text);
  std::string out;
  for (uint8_t i = 0; i < q.length(); i++) {
    const uint32_t u = q.units()[i];
    out += u < 0x80 ? static_cast<char>(u) : '?';
  }
  return out;
}

struct Found {
  uint32_t first;
  uint32_t last;
};

// Feeds lines of words (a line break between vectors) and collects every hit.
std::vector<Found> run(const char* query, const std::vector<std::vector<const char*>>& lines,
                       PhraseMatcher* shared = nullptr, uint32_t* tokenCounter = nullptr) {
  static SearchQuery q;
  EXPECT_TRUE(q.set(query));
  PhraseMatcher local;
  PhraseMatcher& m = shared ? *shared : local;
  if (!shared) m.begin(&q);
  std::vector<Found> hits;
  uint32_t token = tokenCounter ? *tokenCounter : 0;
  bool carry = false;
  for (const auto& line : lines) {
    for (size_t i = 0; i < line.size(); i++) {
      const size_t len = strlen(line[i]);
      if (m.feed(line[i], len, token, carry)) hits.push_back({m.hit().firstToken, m.hit().lastToken});
      carry = i + 1 == line.size() && endsWithHyphenBreak(line[i], len);
      token++;
    }
  }
  if (tokenCounter) *tokenCounter = token;
  return hits;
}

}  // namespace

TEST(SearchFold, AsciiCaseAndSeparators) {
  EXPECT_EQ(foldAll("Of The"), "ofthe");
  EXPECT_EQ(foldAll("  don't, R2-D2! "), "dontr2d2");
  EXPECT_FALSE(SearchQuery().set("!?,;"));
  EXPECT_FALSE(SearchQuery().set(""));
}

TEST(SearchFold, LatinAccents) {
  EXPECT_EQ(foldAll("Éléphant ÇA"), "elephantca");
  EXPECT_EQ(foldAll("Straße"), "strasse");
  EXPECT_EQ(foldAll("Æsir œuvre Þorn"), "aesiroeuvrethorn");
  EXPECT_EQ(foldAll("Łódź Ŝŝ ÿŸ"), "lodzssyy");
  // Decomposed accent (e + U+0301) folds like the precomposed one.
  EXPECT_EQ(foldAll("e\xCC\x81t\xC3\xA9"), "ete");
  // Soft hyphen is invisible, not a word break.
  EXPECT_EQ(foldAll("hy\xC2\xADphen"), "hyphen");
}

TEST(SearchFold, SeparatorsAndOtherScripts) {
  uint32_t out[2];
  bool sep = false;
  EXPECT_EQ(foldCodepoint(0x2019, out, &sep), 0);  // ’
  EXPECT_TRUE(sep);
  EXPECT_EQ(foldCodepoint(0x2014, out, &sep), 0);  // —
  EXPECT_TRUE(sep);
  EXPECT_EQ(foldCodepoint(0xD7, out, &sep), 0);  // ×
  EXPECT_TRUE(sep);
  ASSERT_EQ(foldCodepoint(0x416, out, &sep), 1);  // Ж -> ж
  EXPECT_EQ(out[0], 0x436u);
  ASSERT_EQ(foldCodepoint(0x3A3, out, &sep), 1);  // Σ -> σ
  EXPECT_EQ(out[0], 0x3C3u);
  ASSERT_EQ(foldCodepoint(0x3C2, out, &sep), 1);  // ς -> σ
  EXPECT_EQ(out[0], 0x3C3u);
  ASSERT_EQ(foldCodepoint(0x4E2D, out, &sep), 1);  // 中 stays
  EXPECT_FALSE(sep);
}

TEST(SearchFold, HyphenBreak) {
  EXPECT_TRUE(endsWithHyphenBreak("won-", 4));
  EXPECT_TRUE(endsWithHyphenBreak("caf\xC3\xA9-", 6));
  EXPECT_FALSE(endsWithHyphenBreak("-", 1));
  EXPECT_FALSE(endsWithHyphenBreak("word", 4));
  EXPECT_FALSE(endsWithHyphenBreak("12-", 3));
}

TEST(PhraseMatch, SingleWordPrefixOnly) {
  const auto hits = run("read", {{"bread", "reader", "Read", "unread"}});
  ASSERT_EQ(hits.size(), 2u);
  EXPECT_EQ(hits[0].first, 1u);
  EXPECT_EQ(hits[1].first, 2u);
}

TEST(PhraseMatch, PhraseAcrossWordsAndLines) {
  const auto hits = run("of the", {{"Most", "of"}, {"the", "time"}});
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].first, 1u);
  EXPECT_EQ(hits[0].last, 2u);
}

TEST(PhraseMatch, NoMatchInsideWord) {
  EXPECT_TRUE(run("of the", {{"proof", "theory"}}).empty());
  EXPECT_TRUE(run("the", {{"other", "bathe"}}).empty());
}

TEST(PhraseMatch, PunctuationAndQuotesIgnored) {
  // Tokens as the layout splits them: words, punctuation and quotes apart.
  const auto hits = run("hello world", {{"\xE2\x80\x9C", "Hello", ",", "world", "!", "\xE2\x80\x9D"}});
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].first, 1u);
  EXPECT_EQ(hits[0].last, 3u);
  const auto apostrophe = run("don't", {{"I", "don\xE2\x80\x99t", "know"}});
  ASSERT_EQ(apostrophe.size(), 1u);
  EXPECT_EQ(apostrophe[0].first, 1u);
}

TEST(PhraseMatch, AccentFoldingInText) {
  const auto hits = run("cafe", {{"Le", "Caf\xC3\xA9", "noir"}});
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].first, 1u);
  const auto reverse = run("caf\xC3\xA9", {{"a", "cafe"}});
  ASSERT_EQ(reverse.size(), 1u);
}

TEST(PhraseMatch, WordAfterApostropheIsAStart) {
  const auto hits = run("amour", {{"l\xE2\x80\x99""amour"}});
  ASSERT_EQ(hits.size(), 1u);
}

TEST(PhraseMatch, HyphenationBreakJoins) {
  const auto hits = run("wonderful", {{"a", "won-"}, {"derful", "day"}});
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].first, 1u);
  EXPECT_EQ(hits[0].last, 2u);
  // The continuation is not a word start of its own.
  EXPECT_TRUE(run("derful", {{"won-"}, {"derful"}}).empty());
  // Mid-line hyphenated compounds keep their parts as words.
  EXPECT_EQ(run("known", {{"well", "-", "known"}}).size(), 1u);
  EXPECT_EQ(run("well-known", {{"well", "-"}, {"known"}}).size(), 1u);
}

TEST(PhraseMatch, StateCarriesAcrossFeeds) {
  static SearchQuery q;
  ASSERT_TRUE(q.set("end of page"));
  PhraseMatcher m;
  m.begin(&q);
  uint32_t token = 10;
  EXPECT_TRUE(run("end of page", {{"the", "end"}}, &m, &token).empty());
  const auto hits = run("end of page", {{"of", "page", "two"}}, &m, &token);
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0].first, 11u);
  EXPECT_EQ(hits[0].last, 13u);
  // reset() drops the partial match.
  m.reset();
  EXPECT_TRUE(run("end of page", {{"end"}}, &m, &token).empty());
  m.reset();
  EXPECT_TRUE(run("end of page", {{"of", "page"}}, &m, &token).empty());
}

TEST(PhraseMatch, RepeatedAndOverlapping) {
  const auto hits = run("la la", {{"la", "la", "la", "land"}});
  ASSERT_EQ(hits.size(), 3u);
  EXPECT_EQ(hits[0].first, 0u);
  EXPECT_EQ(hits[1].first, 1u);
  EXPECT_EQ(hits[2].first, 2u);
  EXPECT_EQ(hits[2].last, 3u);
}

TEST(PhraseMatch, Ideographs) {
  // No spaces in CJK: any character can start a match.
  const auto hits = run("\xE4\xB8\xAD\xE6\x96\x87", {{"\xE6\x88\x91\xE5\xAD\xA6\xE4\xB8\xAD\xE6\x96\x87"}});
  ASSERT_EQ(hits.size(), 1u);
}

TEST(PhraseMatch, LongQueryUsesHighBits) {
  std::string longQuery(90, 'a');
  std::string word(90, 'a');
  static SearchQuery q;
  ASSERT_TRUE(q.set(longQuery.c_str()));
  EXPECT_EQ(q.length(), 90);
  PhraseMatcher m;
  m.begin(&q);
  EXPECT_FALSE(m.feed(word.c_str(), 89, 0, false));
  // 89 + the first unit of the next word: the match starts in token 0.
  EXPECT_TRUE(m.feed(word.c_str(), 90, 1, false));
  EXPECT_EQ(m.hit().firstToken, 0u);
  m.reset();
  EXPECT_TRUE(m.feed(word.c_str(), 90, 2, false));
  EXPECT_EQ(m.hit().firstToken, 2u);
  // A 90-unit match split over two tokens: the second is a word start of its
  // own but the match began in the first.
  m.reset();
  EXPECT_FALSE(m.feed(word.c_str(), 50, 7, false));
  EXPECT_TRUE(m.feed(word.c_str(), 40, 8, true));
  EXPECT_EQ(m.hit().firstToken, 7u);
}

TEST(PhraseMatch, QueryCapped) {
  std::string huge(200, 'b');
  SearchQuery q;
  ASSERT_TRUE(q.set(huge.c_str()));
  EXPECT_EQ(q.length(), SearchQuery::MAX_UNITS);
}
