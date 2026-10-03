#include "deckpoint/KeyHelpText.h"

#include <gtest/gtest.h>

using deckpoint::HelpRow;
using deckpoint::LegendSnapshot;

namespace {

LegendSnapshot makeLegend(const char* back, const char* confirm, const char* prev, const char* next,
                          const char* extra = nullptr, bool prevDir = false, bool nextDir = false) {
  LegendSnapshot s{};
  deckpoint::cleanLabelInto(back, s.back, sizeof(s.back));
  deckpoint::cleanLabelInto(confirm, s.confirm, sizeof(s.confirm));
  deckpoint::cleanLabelInto(prev, s.prev, sizeof(s.prev));
  deckpoint::cleanLabelInto(next, s.next, sizeof(s.next));
  deckpoint::splitLegendPart(extra, s.extraKeys, sizeof(s.extraKeys), s.extraWhat, sizeof(s.extraWhat));
  s.prevIsDirection = prevDir;
  s.nextIsDirection = nextDir;
  return s;
}

}  // namespace

TEST(KeyHelpText, CleanLabelDropsDecorationsAndSpaces) {
  char out[32];
  deckpoint::cleanLabelInto("\xC2\xAB Back", out, sizeof(out));
  EXPECT_STREQ(out, "Back");
  deckpoint::cleanLabelInto("Next \xE2\x80\xBA", out, sizeof(out));
  EXPECT_STREQ(out, "Next");
  deckpoint::cleanLabelInto(nullptr, out, sizeof(out));
  EXPECT_STREQ(out, "");
  deckpoint::cleanLabelInto("   ", out, sizeof(out));
  EXPECT_STREQ(out, "");
}

TEST(KeyHelpText, CleanLabelTruncatesOnCodepointBoundary) {
  char out[5];
  // "ab" + U+00E9 (2 bytes) + "c": 4 bytes fit, the é must not be split.
  deckpoint::cleanLabelInto("ab\xC3\xA9" "c", out, sizeof(out));
  EXPECT_STREQ(out, "ab\xC3\xA9");
  char tiny[4];
  deckpoint::cleanLabelInto("ab\xC3\xA9", tiny, sizeof(tiny));
  EXPECT_STREQ(tiny, "ab");
}

TEST(KeyHelpText, SplitLegendPart) {
  char keys[16];
  char what[32];
  deckpoint::splitLegendPart("h/l: tab", keys, sizeof(keys), what, sizeof(what));
  EXPECT_STREQ(keys, "h/l");
  EXPECT_STREQ(what, "tab");
  deckpoint::splitLegendPart("just words", keys, sizeof(keys), what, sizeof(what));
  EXPECT_STREQ(keys, "");
  EXPECT_STREQ(what, "just words");
  deckpoint::splitLegendPart(nullptr, keys, sizeof(keys), what, sizeof(what));
  EXPECT_STREQ(keys, "");
  EXPECT_STREQ(what, "");
}

TEST(KeyHelpText, DirectionalLegendCollapsesToMove) {
  const auto legend = makeLegend("\xC2\xAB Back", "Select", "Up", "Down", "h/l: tab", true, true);
  HelpRow rows[6];
  const int n = deckpoint::legendHelpRows(legend, "Move", rows, 6);
  ASSERT_EQ(n, 4);
  EXPECT_STREQ(rows[0].keys, "Esc");
  EXPECT_STREQ(rows[0].what, "Back");
  EXPECT_STREQ(rows[1].keys, "Enter");
  EXPECT_STREQ(rows[1].what, "Select");
  EXPECT_STREQ(rows[2].keys, "h/l");
  EXPECT_STREQ(rows[2].what, "tab");
  EXPECT_STREQ(rows[3].keys, "j / k");
  EXPECT_STREQ(rows[3].what, "Move");
}

TEST(KeyHelpText, NamedPrevNextMapToKAndJ) {
  const auto legend = makeLegend("Back", "", "Search", "Down", nullptr, false, true);
  HelpRow rows[6];
  const int n = deckpoint::legendHelpRows(legend, "Move", rows, 6);
  ASSERT_EQ(n, 3);
  EXPECT_STREQ(rows[0].keys, "Esc");
  EXPECT_STREQ(rows[1].keys, "k");
  EXPECT_STREQ(rows[1].what, "Search");
  EXPECT_STREQ(rows[2].keys, "j");
  EXPECT_STREQ(rows[2].what, "Down");
}

TEST(KeyHelpText, EmptyLegendHasNoRowsAndRespectsMax) {
  const auto empty = makeLegend("", "", "", "");
  HelpRow rows[6];
  EXPECT_EQ(deckpoint::legendHelpRows(empty, "Move", rows, 6), 0);
  const auto full = makeLegend("Back", "OK", "Up", "Down", nullptr, true, true);
  EXPECT_EQ(deckpoint::legendHelpRows(full, "Move", rows, 2), 2);
}

TEST(KeyHelpText, HumanizeActivityName) {
  char out[40];
  deckpoint::humanizeActivityName("WifiSelection", out, sizeof(out));
  EXPECT_STREQ(out, "Wifi Selection");
  deckpoint::humanizeActivityName("KOReaderSync", out, sizeof(out));
  EXPECT_STREQ(out, "KOReader Sync");
  deckpoint::humanizeActivityName("Opds2Browser", out, sizeof(out));
  EXPECT_STREQ(out, "Opds2 Browser");
  char small[6];
  deckpoint::humanizeActivityName("AbcDef", small, sizeof(small));
  EXPECT_STREQ(small, "Abc D");
}
