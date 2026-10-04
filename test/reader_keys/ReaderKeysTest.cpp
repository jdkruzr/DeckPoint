#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "deckpoint/reader/Marks.h"
#include "deckpoint/reader/ReaderKeys.h"

using deckpoint::reader::MarkPosition;
using deckpoint::reader::MarkTable;
using deckpoint::reader::ReaderCmd;
using deckpoint::reader::ReaderCommand;
using deckpoint::reader::ReaderKeys;
using freeink::KeyEvent;
using freeink::SpecialKey;
namespace codec = deckpoint::reader::marks_codec;

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

// Feeds every char of `keys`; returns the last command.
ReaderCommand type(ReaderKeys& p, const char* keys) {
  ReaderCommand last;
  for (const char* k = keys; *k; ++k) last = p.feed(ch(*k));
  return last;
}

}  // namespace

TEST(ReaderKeys, SingleKeyPageTurns) {
  ReaderKeys p;
  for (char c : std::string("jl ")) {
    const auto cmd = p.feed(ch(c));
    EXPECT_EQ(cmd.type, ReaderCmd::NextPage) << c;
    EXPECT_EQ(cmd.count, 1);
    EXPECT_FALSE(cmd.hasCount);
  }
  for (char c : std::string("khb")) EXPECT_EQ(p.feed(ch(c)).type, ReaderCmd::PrevPage) << c;
  EXPECT_FALSE(p.pending());
}

TEST(ReaderKeys, ArrowAndPageKeys) {
  ReaderKeys p;
  EXPECT_EQ(p.feed(special(SpecialKey::PageDown)).type, ReaderCmd::NextPage);
  EXPECT_EQ(p.feed(special(SpecialKey::Down)).type, ReaderCmd::NextPage);
  EXPECT_EQ(p.feed(special(SpecialKey::Right)).type, ReaderCmd::NextPage);
  EXPECT_EQ(p.feed(special(SpecialKey::PageUp)).type, ReaderCmd::PrevPage);
  EXPECT_EQ(p.feed(special(SpecialKey::Up)).type, ReaderCmd::PrevPage);
  EXPECT_EQ(p.feed(special(SpecialKey::Left)).type, ReaderCmd::PrevPage);
}

TEST(ReaderKeys, CountsApplyToPageKeys) {
  ReaderKeys p;
  EXPECT_EQ(p.feed(ch('5')).type, ReaderCmd::Pending);
  EXPECT_STREQ(p.pendingText(), "5");
  const auto cmd = p.feed(ch('j'));
  EXPECT_EQ(cmd.type, ReaderCmd::NextPage);
  EXPECT_EQ(cmd.count, 5);
  EXPECT_TRUE(cmd.hasCount);
  EXPECT_STREQ(p.pendingText(), "");

  EXPECT_EQ(type(p, "12").type, ReaderCmd::Pending);
  EXPECT_STREQ(p.pendingText(), "12");
  const auto back = p.feed(special(SpecialKey::PageUp));
  EXPECT_EQ(back.type, ReaderCmd::PrevPage);
  EXPECT_EQ(back.count, 12);

  const auto zeroInside = type(p, "10k");
  EXPECT_EQ(zeroInside.type, ReaderCmd::PrevPage);
  EXPECT_EQ(zeroInside.count, 10);
}

TEST(ReaderKeys, CountCapsAt999) {
  ReaderKeys p;
  type(p, "123456");
  EXPECT_STREQ(p.pendingText(), "999");
  const auto cmd = p.feed(ch('j'));
  EXPECT_EQ(cmd.count, ReaderKeys::MAX_COUNT);
}

TEST(ReaderKeys, LeadingZeroIsNotACount) {
  ReaderKeys p;
  EXPECT_EQ(p.feed(ch('0')).type, ReaderCmd::None);
  EXPECT_FALSE(p.pending());
  EXPECT_EQ(p.feed(ch('j')).count, 1);
}

TEST(ReaderKeys, GgAndG) {
  ReaderKeys p;
  EXPECT_EQ(p.feed(ch('g')).type, ReaderCmd::Pending);
  EXPECT_STREQ(p.pendingText(), "g");
  EXPECT_EQ(p.feed(ch('g')).type, ReaderCmd::BookStart);
  EXPECT_EQ(p.feed(ch('G')).type, ReaderCmd::BookEnd);
  // g followed by anything else drops the prefix and the key.
  EXPECT_EQ(type(p, "gj").type, ReaderCmd::None);
  EXPECT_FALSE(p.pending());
}

TEST(ReaderKeys, ChapterJumps) {
  ReaderKeys p;
  EXPECT_EQ(p.feed(ch(']')).type, ReaderCmd::Pending);
  EXPECT_STREQ(p.pendingText(), "]");
  EXPECT_EQ(p.feed(ch(']')).type, ReaderCmd::NextChapter);
  EXPECT_EQ(type(p, "[[").type, ReaderCmd::PrevChapter);
  EXPECT_EQ(type(p, "][").type, ReaderCmd::None);
  EXPECT_FALSE(p.pending());

  type(p, "3]");
  EXPECT_STREQ(p.pendingText(), "3]");
  const auto counted = p.feed(ch(']'));
  EXPECT_EQ(counted.type, ReaderCmd::NextChapter);
  EXPECT_EQ(counted.count, 3);

  EXPECT_EQ(p.feed(ch(')')).type, ReaderCmd::NextChapter);
  const auto paren = type(p, "2(");
  EXPECT_EQ(paren.type, ReaderCmd::PrevChapter);
  EXPECT_EQ(paren.count, 2);
}

TEST(ReaderKeys, Percent) {
  ReaderKeys p;
  const auto pct = type(p, "42%");
  EXPECT_EQ(pct.type, ReaderCmd::GoPercent);
  EXPECT_EQ(pct.count, 42);

  const auto viaG = type(p, "75G");
  EXPECT_EQ(viaG.type, ReaderCmd::GoPercent);
  EXPECT_EQ(viaG.count, 75);

  const auto capped = type(p, "250%");
  EXPECT_EQ(capped.type, ReaderCmd::GoPercent);
  EXPECT_EQ(capped.count, ReaderKeys::MAX_PERCENT);

  // % with no count is not a command.
  EXPECT_EQ(p.feed(ch('%')).type, ReaderCmd::None);
}

TEST(ReaderKeys, Marks) {
  ReaderKeys p;
  EXPECT_EQ(p.feed(ch('m')).type, ReaderCmd::Pending);
  EXPECT_STREQ(p.pendingText(), "m");
  const auto set = p.feed(ch('a'));
  EXPECT_EQ(set.type, ReaderCmd::SetMark);
  EXPECT_EQ(set.arg, 'a');

  EXPECT_EQ(p.feed(ch('\'')).type, ReaderCmd::Pending);
  EXPECT_STREQ(p.pendingText(), "'");
  const auto jump = p.feed(ch('z'));
  EXPECT_EQ(jump.type, ReaderCmd::JumpMark);
  EXPECT_EQ(jump.arg, 'z');

  EXPECT_EQ(type(p, "''").type, ReaderCmd::JumpBack);

  // Only a-z name marks.
  EXPECT_EQ(type(p, "mA").type, ReaderCmd::None);
  EXPECT_EQ(type(p, "m1").type, ReaderCmd::None);
  EXPECT_EQ(type(p, "'G").type, ReaderCmd::None);
  EXPECT_FALSE(p.pending());
}

TEST(ReaderKeys, EscapeClearsPendingThenGoesBack) {
  ReaderKeys p;
  type(p, "12");
  EXPECT_EQ(p.feed(special(SpecialKey::Escape)).type, ReaderCmd::Cancel);
  EXPECT_FALSE(p.pending());
  EXPECT_EQ(p.feed(special(SpecialKey::Escape)).type, ReaderCmd::Back);

  p.feed(ch('m'));
  EXPECT_EQ(p.feed(special(SpecialKey::Backspace)).type, ReaderCmd::Cancel);
  EXPECT_EQ(p.feed(special(SpecialKey::Backspace)).type, ReaderCmd::Back);

  // The count does not survive a cancel.
  type(p, "5");
  p.feed(special(SpecialKey::Escape));
  EXPECT_EQ(p.feed(ch('j')).count, 1);
}

TEST(ReaderKeys, UnknownKeysAreIgnoredAndClearPending) {
  ReaderKeys p;
  EXPECT_EQ(p.feed(ch('x')).type, ReaderCmd::None);
  type(p, "7");
  EXPECT_EQ(p.feed(ch('q')).type, ReaderCmd::None);
  EXPECT_FALSE(p.pending());
  EXPECT_EQ(p.feed(ch('j')).count, 1);
  type(p, "g");
  EXPECT_EQ(p.feed(special(SpecialKey::Tab)).type, ReaderCmd::None);
  EXPECT_FALSE(p.pending());
}

TEST(ReaderKeys, HelpPassesThroughAndResets) {
  ReaderKeys p;
  EXPECT_EQ(p.feed(ch('?')).type, ReaderCmd::Help);
  type(p, "3g");
  EXPECT_EQ(p.feed(ch('?')).type, ReaderCmd::Help);
  EXPECT_FALSE(p.pending());
}

TEST(ReaderKeys, AltVIsHelpPlainVIsNot) {
  ReaderKeys p;
  KeyEvent altV = ch('v');
  altV.mods = freeink::KeyMod::Alt;
  EXPECT_EQ(p.feed(altV).type, ReaderCmd::Help);
  EXPECT_NE(p.feed(ch('v')).type, ReaderCmd::Help);
}

TEST(ReaderKeys, VStartsHighlight) {
  ReaderKeys p;
  EXPECT_EQ(p.feed(ch('v')).type, ReaderCmd::Highlight);
  // A half-typed prefix is dropped, not turned into a highlight.
  EXPECT_EQ(p.feed(ch('g')).type, ReaderCmd::Pending);
  EXPECT_EQ(p.feed(ch('v')).type, ReaderCmd::None);
}

TEST(ReaderKeys, SingleCommands) {
  ReaderKeys p;
  EXPECT_EQ(p.feed(ch('t')).type, ReaderCmd::Toc);
  EXPECT_EQ(p.feed(ch('B')).type, ReaderCmd::ToggleBookmark);
  EXPECT_EQ(p.feed(special(SpecialKey::Enter)).type, ReaderCmd::Menu);
  EXPECT_EQ(p.feed(ch('d')).type, ReaderCmd::Dictionary);
  EXPECT_EQ(p.feed(ch('D')).type, ReaderCmd::LookupWord);
  EXPECT_EQ(p.feed(ch('/')).type, ReaderCmd::Search);
  EXPECT_EQ(p.feed(ch('n')).type, ReaderCmd::SearchNext);
  EXPECT_EQ(p.feed(ch('N')).type, ReaderCmd::SearchPrev);
  EXPECT_EQ(p.feed(ch(':')).type, ReaderCmd::CommandLine);
  // Enter mid-prefix still opens the menu and drops the prefix.
  type(p, "4g");
  EXPECT_EQ(p.feed(special(SpecialKey::Enter)).type, ReaderCmd::Menu);
  EXPECT_FALSE(p.pending());
}

TEST(Marks, TableSetAndGet) {
  MarkTable t;
  EXPECT_FALSE(t.isSet('a'));
  EXPECT_EQ(t.get('a'), nullptr);
  MarkPosition pos;
  pos.spineIndex = 3;
  pos.page = 17;
  t.set('c', pos);
  ASSERT_NE(t.get('c'), nullptr);
  EXPECT_EQ(t.get('c')->page, 17);
  EXPECT_FALSE(t.isSet('A'));
  t.set('A', pos);  // ignored
  EXPECT_EQ(t.setMask, 1u << 2);
}

TEST(Marks, CodecRoundTrip) {
  MarkTable t;
  MarkPosition a;
  a.spineIndex = 513;
  a.page = 65535;
  a.hasVisibleTextOffset = true;
  a.visibleTextOffset = 0x12345678;
  MarkPosition z;
  z.spineIndex = 2;
  z.page = 9;
  t.set('a', a);
  t.set('z', z);

  uint8_t buf[codec::FILE_SIZE];
  codec::encode(t, buf);
  MarkTable out;
  ASSERT_TRUE(codec::decode(buf, sizeof(buf), out));
  EXPECT_EQ(out.setMask, t.setMask);
  ASSERT_NE(out.get('a'), nullptr);
  EXPECT_EQ(out.get('a')->spineIndex, 513);
  EXPECT_EQ(out.get('a')->page, 65535);
  EXPECT_TRUE(out.get('a')->hasVisibleTextOffset);
  EXPECT_EQ(out.get('a')->visibleTextOffset, 0x12345678u);
  ASSERT_NE(out.get('z'), nullptr);
  EXPECT_FALSE(out.get('z')->hasVisibleTextOffset);
  EXPECT_EQ(out.get('z')->page, 9);
  EXPECT_EQ(out.get('m'), nullptr);
}

TEST(Marks, CodecRejectsBadInput) {
  MarkTable t;
  t.set('b', MarkPosition{});
  uint8_t buf[codec::FILE_SIZE];
  codec::encode(t, buf);

  MarkTable out;
  out.set('q', MarkPosition{});
  EXPECT_FALSE(codec::decode(buf, sizeof(buf) - 1, out));
  EXPECT_TRUE(out.isSet('q'));  // untouched on failure

  uint8_t bad[codec::FILE_SIZE];
  std::memcpy(bad, buf, sizeof(bad));
  bad[2] = codec::VERSION + 1;
  EXPECT_FALSE(codec::decode(bad, sizeof(bad), out));
  std::memcpy(bad, buf, sizeof(bad));
  bad[0] = 'X';
  EXPECT_FALSE(codec::decode(bad, sizeof(bad), out));
  EXPECT_FALSE(codec::decode(nullptr, 0, out));
}
