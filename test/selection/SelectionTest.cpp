#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "deckpoint/reader/SelectionSession.h"

using deckpoint::reader::ActionMenu;
using deckpoint::reader::BarLayout;
using deckpoint::reader::layoutBar;
using deckpoint::reader::layoutMenu;
using deckpoint::reader::MenuLayout;
using deckpoint::reader::OffsetRange;
using deckpoint::reader::SelectionAction;
using deckpoint::reader::SelectionSession;
using deckpoint::reader::SelectionText;
using deckpoint::reader::WordExtent;
using deckpoint::reader::wordExtent;
using freeink::KeyEvent;
using freeink::SpecialKey;
using Stage = SelectionSession::Stage;

namespace {

KeyEvent ch(char c) {
  KeyEvent e;
  e.ch = c;
  return e;
}

KeyEvent special(SpecialKey k) {
  KeyEvent e;
  e.special = k;
  return e;
}

// "The quick brown fox, jumps." as tokens at their source offsets.
struct Token {
  uint32_t offset;
  const char* text;
};
const std::vector<Token> TOKENS = {
    {0, "The"}, {4, "quick"}, {10, "brown"}, {16, "fox,"}, {21, "jumps."},
};

std::vector<WordExtent> extents() {
  std::vector<WordExtent> out;
  for (const auto& t : TOKENS) out.push_back(wordExtent(t.offset, t.text, strlen(t.text)));
  return out;
}

std::string textFor(const OffsetRange r, const size_t maxBytes = 1024) {
  std::string out;
  SelectionText builder(r, maxBytes, out);
  for (const auto& t : TOKENS) builder.add(t.offset, t.text, strlen(t.text));
  return out;
}

}  // namespace

// --- SelectionSession --------------------------------------------------------

TEST(SelectionSession, KeyboardStartThenEnd) {
  SelectionSession s;
  EXPECT_EQ(s.stage(), Stage::Idle);
  EXPECT_FALSE(s.pick(2));  // idle: nothing to pick for
  s.begin();
  EXPECT_EQ(s.stage(), Stage::PickStart);
  EXPECT_EQ(s.first(), -1);
  EXPECT_TRUE(s.pick(1));
  EXPECT_EQ(s.stage(), Stage::PickEnd);
  EXPECT_EQ(s.anchor(), 1);
  EXPECT_EQ(s.first(), 1);
  EXPECT_EQ(s.last(), 1);
  EXPECT_TRUE(s.pick(3));
  EXPECT_EQ(s.stage(), Stage::Range);
  EXPECT_EQ(s.first(), 1);
  EXPECT_EQ(s.last(), 3);
  EXPECT_TRUE(s.covers(2));
  EXPECT_FALSE(s.covers(4));
  const auto e = extents();
  const OffsetRange r = s.range(e.data(), e.size());
  EXPECT_EQ(r.start, 4u);
  EXPECT_EQ(r.end, 20u);  // end of "fox," (glued comma included)
}

TEST(SelectionSession, ReversedPicksGiveTheSameRange) {
  SelectionSession s;
  s.begin();
  s.pick(3);
  s.pick(1);
  EXPECT_EQ(s.anchor(), 3);
  EXPECT_EQ(s.head(), 1);
  EXPECT_EQ(s.first(), 1);
  EXPECT_EQ(s.last(), 3);
  const auto e = extents();
  const OffsetRange r = s.range(e.data(), e.size());
  EXPECT_EQ(r.start, 4u);
  EXPECT_EQ(r.end, 20u);
}

TEST(SelectionSession, SingleWordFromEnterOrSameWord) {
  const auto e = extents();
  SelectionSession s;
  s.begin();
  EXPECT_FALSE(s.pickSingle());  // no anchor yet
  s.pick(2);
  EXPECT_TRUE(s.pickSingle());
  EXPECT_EQ(s.stage(), Stage::Range);
  OffsetRange r = s.range(e.data(), e.size());
  EXPECT_EQ(r.start, 10u);
  EXPECT_EQ(r.end, 15u);

  s.begin();
  s.pick(4);
  s.pick(4);
  r = s.range(e.data(), e.size());
  EXPECT_EQ(r.start, 21u);
  EXPECT_EQ(r.end, 27u);
}

TEST(SelectionSession, TouchStartsWithAnchorAndRepicksHead) {
  SelectionSession s;
  s.beginAt(2);
  EXPECT_EQ(s.stage(), Stage::PickEnd);
  EXPECT_EQ(s.anchor(), 2);
  s.pick(4);
  EXPECT_EQ(s.last(), 4);
  s.pick(0);  // a new end replaces the head; the anchor stays
  EXPECT_EQ(s.stage(), Stage::Range);
  EXPECT_EQ(s.first(), 0);
  EXPECT_EQ(s.last(), 2);
  s.beginAt(-1);
  EXPECT_EQ(s.stage(), Stage::PickStart);
}

TEST(SelectionSession, BackStepsOneStage) {
  SelectionSession s;
  s.begin();
  EXPECT_FALSE(s.back());
  s.pick(1);
  s.pick(2);
  EXPECT_TRUE(s.back());
  EXPECT_EQ(s.stage(), Stage::PickEnd);
  EXPECT_EQ(s.head(), -1);
  EXPECT_EQ(s.anchor(), 1);
  EXPECT_TRUE(s.back());
  EXPECT_EQ(s.stage(), Stage::PickStart);
  EXPECT_EQ(s.anchor(), -1);
  EXPECT_FALSE(s.pick(-1));
  EXPECT_EQ(s.stage(), Stage::PickStart);
}

TEST(SelectionSession, OutOfRangeIndexGivesEmptyRange) {
  SelectionSession s;
  s.begin();
  s.pick(1);
  s.pick(9);
  const auto e = extents();
  EXPECT_TRUE(s.range(e.data(), e.size()).empty());
  s.reset();
  EXPECT_TRUE(s.range(e.data(), e.size()).empty());
}

TEST(SelectionSession, SpanTakesOuterBounds) {
  const OffsetRange r = SelectionSession::span({30, 35}, {10, 14});
  EXPECT_EQ(r.start, 10u);
  EXPECT_EQ(r.end, 35u);
  EXPECT_TRUE(r.contains(10));
  EXPECT_FALSE(r.contains(35));
}

TEST(SelectionSession, WordExtentSkipsIndentAndCountsCodepoints) {
  // Leading U+2003 indent does not count; "é" is one codepoint.
  const char* indented =
      "\xE2\x80\x83"
      "Caf\xC3\xA9";
  const WordExtent e = wordExtent(100, indented, strlen(indented));
  EXPECT_EQ(e.start, 100u);
  EXPECT_EQ(e.end, 104u);
}

// --- SelectionText -----------------------------------------------------------

TEST(SelectionText, JoinsWithSpacesWhereTheSourceHasThem) {
  EXPECT_EQ(textFor({4, 20}), "quick brown fox,");
  EXPECT_EQ(textFor({0, 27}), "The quick brown fox, jumps.");
  EXPECT_EQ(textFor({10, 15}), "brown");
}

TEST(SelectionText, GluedTokensStayGlued) {
  std::string out;
  SelectionText builder({0, 20}, 1024, out);
  builder.add(0, "prisoners", 9);
  builder.add(9, "\xE2\x80\xA6", 3);  // … right after, no space in the source
  builder.add(10, "we", 2);
  EXPECT_EQ(out, "prisoners\xE2\x80\xA6we");
}

TEST(SelectionText, DropsIndentAndStopsAtWholeTokens) {
  std::string out;
  SelectionText builder({0, 100}, 9, out);
  builder.add(0, "\xE2\x80\x83One", 6);
  builder.add(4, "two", 3);
  builder.add(8, "three", 5);  // would make "One two three" (13 > 9)
  builder.add(14, "x", 1);     // nothing after the cut
  EXPECT_EQ(out, "One two");
  EXPECT_TRUE(builder.truncated());
}

// --- ActionMenu --------------------------------------------------------------

TEST(ActionMenu, WordAndHighlightItems) {
  ActionMenu m;
  m.open(ActionMenu::Kind::Word);
  ASSERT_EQ(m.count(), 3);
  EXPECT_EQ(m.item(0), SelectionAction::LookUp);
  EXPECT_EQ(m.item(1), SelectionAction::Highlight);
  EXPECT_EQ(m.item(2), SelectionAction::Note);
  EXPECT_EQ(m.item(3), SelectionAction::None);
  m.open(ActionMenu::Kind::Highlight);
  EXPECT_EQ(m.item(0), SelectionAction::EditNote);
  EXPECT_EQ(m.item(1), SelectionAction::Delete);
  EXPECT_EQ(m.item(2), SelectionAction::LookUp);
}

TEST(ActionMenu, CursorWrapsAndEnterChooses) {
  ActionMenu m;
  m.open(ActionMenu::Kind::Word);
  EXPECT_EQ(m.feed(ch('k')), ActionMenu::Result::Moved);
  EXPECT_EQ(m.cursor(), 2);
  EXPECT_EQ(m.feed(ch('j')), ActionMenu::Result::Moved);
  EXPECT_EQ(m.cursor(), 0);
  EXPECT_EQ(m.feed(special(SpecialKey::Down)), ActionMenu::Result::Moved);
  EXPECT_EQ(m.feed(special(SpecialKey::Enter)), ActionMenu::Result::Chosen);
  EXPECT_EQ(m.chosen(), SelectionAction::Highlight);
}

TEST(ActionMenu, ShortcutsChooseTheirRow) {
  ActionMenu m;
  m.open(ActionMenu::Kind::Word);
  EXPECT_EQ(m.feed(ch('n')), ActionMenu::Result::Chosen);
  EXPECT_EQ(m.chosen(), SelectionAction::Note);
  m.open(ActionMenu::Kind::Highlight);
  EXPECT_EQ(m.feed(ch('X')), ActionMenu::Result::Chosen);  // case-blind
  EXPECT_EQ(m.chosen(), SelectionAction::Delete);
  m.open(ActionMenu::Kind::Highlight);
  EXPECT_EQ(m.feed(ch('v')), ActionMenu::Result::Ignored);  // no Highlight row here
  EXPECT_EQ(m.feed(ch('d')), ActionMenu::Result::Chosen);
  EXPECT_EQ(m.chosen(), SelectionAction::LookUp);
}

TEST(ActionMenu, EscAndBackspaceCancel) {
  ActionMenu m;
  m.open(ActionMenu::Kind::Word);
  EXPECT_EQ(m.feed(special(SpecialKey::Escape)), ActionMenu::Result::Cancelled);
  EXPECT_EQ(m.feed(special(SpecialKey::Backspace)), ActionMenu::Result::Cancelled);
  EXPECT_EQ(m.feed(ch('q')), ActionMenu::Result::Ignored);
}

TEST(ActionMenu, TouchChooseChecksRange) {
  ActionMenu m;
  m.open(ActionMenu::Kind::Word);
  EXPECT_FALSE(m.choose(3));
  EXPECT_FALSE(m.choose(-1));
  EXPECT_TRUE(m.choose(0));
  EXPECT_EQ(m.chosen(), SelectionAction::LookUp);
}

// --- layout ------------------------------------------------------------------

TEST(MenuLayout, BelowTheLineWhenItFits) {
  // 240x320, legend band 16, word line at y=40 (17 px), 3 rows of 24 + 2 px top.
  const MenuLayout l = layoutMenu(240, 320, 16, 40, 17, 224, 2, 24, 3);
  EXPECT_EQ(l.box.x, 8);
  EXPECT_EQ(l.box.y, 57);
  EXPECT_EQ(l.box.h, 2 + 72);
  EXPECT_EQ(l.hit(10, 57 + 2 + 1), 0);
  EXPECT_EQ(l.hit(10, 57 + 2 + 24), 1);  // rows are contiguous: full pitch
  EXPECT_EQ(l.hit(10, 57 + 2 + 71), 2);
  EXPECT_EQ(l.hit(10, 57), MenuLayout::CHROME);
  EXPECT_EQ(l.hit(4, 60), MenuLayout::OUTSIDE);
  EXPECT_EQ(l.hit(10, 57 + 74), MenuLayout::OUTSIDE);
}

TEST(MenuLayout, AboveTheLineNearTheBottom) {
  const MenuLayout l = layoutMenu(240, 320, 16, 260, 17, 224, 2, 24, 3);
  EXPECT_EQ(l.box.y + l.box.h, 260);
}

TEST(MenuLayout, PinnedWhenNeitherFits) {
  const MenuLayout l = layoutMenu(240, 120, 16, 40, 17, 224, 30, 24, 3);
  EXPECT_EQ(l.box.y, 2);  // 102 px box: no room below (57+102 > 104) nor above (40 < 102)
}

TEST(BarLayout, AwayFromTheAnchor) {
  const BarLayout low = layoutBar(240, 320, 16, 250, 24);
  EXPECT_EQ(low.cancel.y, 0);
  const BarLayout high = layoutBar(240, 320, 16, 30, 24);
  EXPECT_EQ(high.cancel.y, 320 - 16 - 24);
  EXPECT_EQ(high.hit(10, high.cancel.y + 5), BarLayout::CANCEL);
  EXPECT_EQ(high.hit(200, high.cancel.y + 5), BarLayout::CONFIRM);
  EXPECT_EQ(high.hit(200, 10), BarLayout::NONE);
  EXPECT_EQ(high.cancel.w + high.confirm.w, 240);
}
