#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "deckpoint/CommandLine.h"
#include "deckpoint/Commands.h"

using deckpoint::CommandLine;
using deckpoint::CommandResult;
using deckpoint::CommandSpec;
using deckpoint::NameMatch;
using deckpoint::NamePicker;
using deckpoint::ParseKind;
using freeink::KeyEvent;
using freeink::SpecialKey;
namespace KeyMod = freeink::KeyMod;

namespace {

CommandResult noop(void*, const char*, char*, size_t) { return CommandResult::Restore; }

// Same shape as the reader's registry, incl. prefix-sharing names.
constexpr CommandSpec TABLE[] = {
    {"toc", "t", nullptr, StrId::STR_CROSSPOINT, &noop},
    {"bookmarks", "bm", nullptr, StrId::STR_CROSSPOINT, &noop},
    {"bookmark", "mark", nullptr, StrId::STR_CROSSPOINT, &noop},
    {"goto", "g", "N", StrId::STR_CROSSPOINT, &noop},
    {"dict", nullptr, "word", StrId::STR_CROSSPOINT, &noop},
    {"font", nullptr, "name", StrId::STR_CROSSPOINT, &noop},
    {"home", nullptr, nullptr, StrId::STR_CROSSPOINT, &noop},
    {"help", nullptr, nullptr, StrId::STR_CROSSPOINT, &noop},
    {"night", nullptr, nullptr, StrId::STR_CROSSPOINT, &noop},
};
constexpr size_t COUNT = sizeof(TABLE) / sizeof(TABLE[0]);

int indexOf(const char* name) {
  for (size_t i = 0; i < COUNT; ++i) {
    if (strcmp(TABLE[i].name, name) == 0) return static_cast<int>(i);
  }
  return -1;
}

deckpoint::ParsedCommand parse(const char* line) { return deckpoint::parseCommandLine(line, TABLE, COUNT); }

KeyEvent ch(char c, uint8_t mods = 0) {
  KeyEvent e;
  e.ch = c;
  e.mods = mods;
  return e;
}

KeyEvent special(SpecialKey k, uint8_t mods = 0) {
  KeyEvent e;
  e.special = k;
  e.mods = mods;
  return e;
}

void type(CommandLine& line, const char* text) {
  for (const char* p = text; *p; ++p) line.feed(ch(*p));
}

}  // namespace

// --- parsing --------------------------------------------------------------

TEST(CommandParse, BlankLineIsEmpty) {
  EXPECT_EQ(parse("").kind, ParseKind::Empty);
  EXPECT_EQ(parse("   ").kind, ParseKind::Empty);
}

TEST(CommandParse, ExactNameAndAliasCaseInsensitive) {
  auto p = parse("toc");
  EXPECT_EQ(p.kind, ParseKind::Command);
  EXPECT_EQ(p.index, indexOf("toc"));
  EXPECT_EQ(parse("  TOC  ").index, indexOf("toc"));
  EXPECT_EQ(parse("t").index, indexOf("toc"));
  EXPECT_EQ(parse("BM").index, indexOf("bookmarks"));
  EXPECT_EQ(parse("mark").index, indexOf("bookmark"));
}

TEST(CommandParse, ExactNameBeatsLongerPrefixMatch) {
  // "bookmark" is also a prefix of "bookmarks".
  auto p = parse("bookmark");
  EXPECT_EQ(p.kind, ParseKind::Command);
  EXPECT_EQ(p.index, indexOf("bookmark"));
}

TEST(CommandParse, UniquePrefixMatches) {
  EXPECT_EQ(parse("go 50").index, indexOf("goto"));
  EXPECT_EQ(parse("ni").index, indexOf("night"));
  EXPECT_EQ(parse("di cat").index, indexOf("dict"));
}

TEST(CommandParse, AmbiguousPrefix) {
  auto p = parse("h");
  EXPECT_EQ(p.kind, ParseKind::Ambiguous);
  EXPECT_STREQ(p.name(), "h");
  EXPECT_EQ(parse("book").kind, ParseKind::Ambiguous);
}

TEST(CommandParse, UnknownKeepsTheToken) {
  auto p = parse("frobnicate now");
  EXPECT_EQ(p.kind, ParseKind::Unknown);
  EXPECT_STREQ(p.name(), "frobnicate");
}

TEST(CommandParse, OverlongTokenIsUnknown) {
  EXPECT_EQ(parse("tocccccccccccccccccccccccccccccccc").kind, ParseKind::Unknown);
}

TEST(CommandParse, ArgumentsAreTrimmed) {
  auto p = parse("  dict   ice cream   ");
  EXPECT_EQ(p.kind, ParseKind::Command);
  EXPECT_STREQ(p.args(), "ice cream");
  EXPECT_STREQ(parse("toc").args(), "");
}

TEST(CommandParse, ResultSurvivesCopy) {
  deckpoint::ParsedCommand copy;
  copy = parse("font  Noto Sans ");
  EXPECT_STREQ(copy.args(), "Noto Sans");
  EXPECT_STREQ(copy.name(), "font");
}

TEST(CommandParse, NumberIsPercent) {
  auto pct = parse(" 42 ");
  EXPECT_EQ(pct.kind, ParseKind::Percent);
  EXPECT_EQ(pct.value, 42);
  // '%' is optional (the built-in keyboard has none; BLE keyboards may type it).
  EXPECT_EQ(parse("42%").kind, ParseKind::Percent);
  EXPECT_EQ(parse("42%").value, 42);
  EXPECT_EQ(parse("0").kind, ParseKind::Percent);
  EXPECT_EQ(parse("0").value, 0);
  EXPECT_EQ(parse("100").value, 100);
  EXPECT_EQ(parse("007").value, 7);
}

TEST(CommandParse, PagePrefix) {
  auto p = parse("p42");
  EXPECT_EQ(p.kind, ParseKind::Page);
  EXPECT_EQ(p.value, 42);
  EXPECT_EQ(parse("#42").kind, ParseKind::Page);  // Sym+q
  EXPECT_EQ(parse("# 7").value, 7);
  EXPECT_EQ(parse("P9999").value, 9999);
  EXPECT_EQ(parse("p0").kind, ParseKind::BadNumber);
  EXPECT_EQ(parse("p10000").kind, ParseKind::BadNumber);
  EXPECT_EQ(parse("#").kind, ParseKind::BadNumber);
  EXPECT_EQ(parse("p42%").kind, ParseKind::BadNumber);
  EXPECT_EQ(parse("p").kind, ParseKind::Unknown);  // a name, not a page
}

TEST(CommandParse, BadNumbers) {
  EXPECT_EQ(parse("101").kind, ParseKind::BadNumber);
  EXPECT_EQ(parse("101%").kind, ParseKind::BadNumber);
  EXPECT_EQ(parse("1000").kind, ParseKind::BadNumber);
  EXPECT_EQ(parse("12x").kind, ParseKind::BadNumber);
  EXPECT_EQ(parse("5%%").kind, ParseKind::BadNumber);
  EXPECT_EQ(parse("9999999").kind, ParseKind::BadNumber);
}

TEST(NumberTarget, GotoArguments) {
  EXPECT_EQ(deckpoint::parseNumberTarget("50").kind, ParseKind::Percent);
  EXPECT_EQ(deckpoint::parseNumberTarget("50%").kind, ParseKind::Percent);
  EXPECT_EQ(deckpoint::parseNumberTarget("7").value, 7);
  EXPECT_EQ(deckpoint::parseNumberTarget("p12").kind, ParseKind::Page);
  EXPECT_EQ(deckpoint::parseNumberTarget("#12").value, 12);
  EXPECT_EQ(deckpoint::parseNumberTarget("").kind, ParseKind::Empty);
  EXPECT_EQ(deckpoint::parseNumberTarget("half").kind, ParseKind::BadNumber);
  EXPECT_EQ(deckpoint::parseNumberTarget("-3").kind, ParseKind::BadNumber);
}

// --- completion -----------------------------------------------------------

TEST(CommandComplete, UniqueCompletesWithSpace) {
  char out[64];
  char cands[64];
  EXPECT_EQ(deckpoint::completeCommand("go", TABLE, COUNT, out, sizeof(out), cands, sizeof(cands)), 1u);
  EXPECT_STREQ(out, "goto ");
  EXPECT_STREQ(cands, "goto");
  EXPECT_EQ(deckpoint::completeCommand("NI", TABLE, COUNT, out, sizeof(out), cands, sizeof(cands)), 1u);
  EXPECT_STREQ(out, "night ");
}

TEST(CommandComplete, SeveralExtendToCommonPrefixAndListCandidates) {
  char out[64];
  char cands[64];
  EXPECT_EQ(deckpoint::completeCommand("b", TABLE, COUNT, out, sizeof(out), cands, sizeof(cands)), 2u);
  EXPECT_STREQ(out, "bookmark");
  EXPECT_STREQ(cands, "bookmarks  bookmark");
  EXPECT_EQ(deckpoint::completeCommand("h", TABLE, COUNT, out, sizeof(out), cands, sizeof(cands)), 2u);
  EXPECT_STREQ(out, "");  // "h" is already the common prefix
  EXPECT_STREQ(cands, "home  help");
}

TEST(CommandComplete, EmptyLineListsEverything) {
  char out[64];
  char cands[16];
  EXPECT_EQ(deckpoint::completeCommand("", TABLE, COUNT, out, sizeof(out), cands, sizeof(cands)), COUNT);
  EXPECT_STREQ(out, "");
  EXPECT_LT(strlen(cands), sizeof(cands));  // truncated to fit
  EXPECT_STREQ(cands, "toc  bookmarks");  // stops at the first name that does not fit
}

TEST(CommandComplete, NothingAfterArgumentsStartOrNoMatch) {
  char out[64];
  char cands[64];
  EXPECT_EQ(deckpoint::completeCommand("goto 5", TABLE, COUNT, out, sizeof(out), cands, sizeof(cands)), 0u);
  EXPECT_STREQ(out, "");
  EXPECT_EQ(deckpoint::completeCommand("zz", TABLE, COUNT, out, sizeof(out), cands, sizeof(cands)), 0u);
  EXPECT_STREQ(cands, "");
}

// --- name matching (font argument) -----------------------------------------

TEST(NameMatch, Qualities) {
  EXPECT_EQ(deckpoint::matchName("noto serif", "Noto Serif"), NameMatch::Exact);
  EXPECT_EQ(deckpoint::matchName("NotoSerif", "Noto Serif"), NameMatch::Exact);
  EXPECT_EQ(deckpoint::matchName("noto-se", "Noto Serif"), NameMatch::Prefix);
  EXPECT_EQ(deckpoint::matchName("serif", "Noto Serif"), NameMatch::Substring);
  EXPECT_EQ(deckpoint::matchName("mono", "Noto Serif"), NameMatch::None);
  EXPECT_EQ(deckpoint::matchName("", "Noto Serif"), NameMatch::None);
}

TEST(NamePicker, BestQualityWinsAndTiesAreAmbiguous) {
  const char* fonts[] = {"Noto Serif", "Noto Sans", "NotoSansCJK", "Literata"};
  {
    NamePicker p("noto sans");
    for (int i = 0; i < 4; i++) p.add(i, fonts[i]);
    EXPECT_EQ(p.quality(), NameMatch::Exact);
    EXPECT_EQ(p.count(), 1);
    EXPECT_EQ(p.index(), 1);
  }
  {
    NamePicker p("noto");
    for (int i = 0; i < 4; i++) p.add(i, fonts[i]);
    EXPECT_EQ(p.quality(), NameMatch::Prefix);
    EXPECT_EQ(p.count(), 3);
  }
  {
    NamePicker p("lit");
    for (int i = 0; i < 4; i++) p.add(i, fonts[i]);
    EXPECT_EQ(p.count(), 1);
    EXPECT_EQ(p.index(), 3);
  }
  {
    NamePicker p("cjk");
    for (int i = 0; i < 4; i++) p.add(i, fonts[i]);
    EXPECT_EQ(p.quality(), NameMatch::Substring);
    EXPECT_EQ(p.index(), 2);
  }
  {
    NamePicker p("garamond");
    for (int i = 0; i < 4; i++) p.add(i, fonts[i]);
    EXPECT_EQ(p.count(), 0);
    EXPECT_EQ(p.index(), -1);
  }
}

// --- line editing ---------------------------------------------------------

TEST(CommandLineEdit, TypingAndPrefill) {
  CommandLine line;
  EXPECT_FALSE(line.isOpen());
  line.open("dict ");
  EXPECT_TRUE(line.isOpen());
  EXPECT_EQ(line.feed(ch('c')), CommandLine::Action::Edited);
  type(line, "at");
  EXPECT_STREQ(line.text(), "dict cat");
  // Shift / Sym layers arrive already applied in ch.
  EXPECT_EQ(line.feed(ch('!', KeyMod::Shift)), CommandLine::Action::Edited);
  EXPECT_STREQ(line.text(), "dict cat!");
}

TEST(CommandLineEdit, EnterEscAndBackspace) {
  CommandLine line;
  line.open();
  type(line, "ab");
  EXPECT_EQ(line.feed(special(SpecialKey::Backspace)), CommandLine::Action::Edited);
  EXPECT_STREQ(line.text(), "a");
  EXPECT_EQ(line.feed(special(SpecialKey::Delete)), CommandLine::Action::Edited);  // Shift+Backspace
  EXPECT_STREQ(line.text(), "");
  EXPECT_EQ(line.feed(special(SpecialKey::Backspace)), CommandLine::Action::Cancel);
  EXPECT_EQ(line.feed(special(SpecialKey::Escape)), CommandLine::Action::Cancel);
  EXPECT_EQ(line.feed(special(SpecialKey::Enter)), CommandLine::Action::Submit);
}

TEST(CommandLineEdit, AltBackspaceClears) {
  CommandLine line;
  line.open("goto 5");
  EXPECT_EQ(line.feed(special(SpecialKey::Backspace, KeyMod::Alt)), CommandLine::Action::Edited);
  EXPECT_STREQ(line.text(), "");
  EXPECT_EQ(line.feed(special(SpecialKey::Backspace, KeyMod::Alt)), CommandLine::Action::None);
}

TEST(CommandLineEdit, BackspaceDropsWholeUtf8Character) {
  CommandLine line;
  line.open("a\xC3\xA9");  // "aé"
  EXPECT_EQ(line.feed(special(SpecialKey::Backspace)), CommandLine::Action::Edited);
  EXPECT_STREQ(line.text(), "a");
}

TEST(CommandLineEdit, CompletionKeys) {
  CommandLine line;
  line.open();
  EXPECT_EQ(line.feed(ch(' ', KeyMod::Alt)), CommandLine::Action::Complete);  // T-Deck Pro
  EXPECT_EQ(line.feed(special(SpecialKey::Tab)), CommandLine::Action::Complete);  // BLE keyboards
  EXPECT_STREQ(line.text(), "");
}

TEST(CommandLineEdit, HistoryRecall) {
  CommandLine line;
  line.open();
  EXPECT_EQ(line.feed(ch('k', KeyMod::Alt)), CommandLine::Action::None);  // nothing remembered yet
  type(line, "goto 50");
  line.remember();
  line.close();

  line.open();
  EXPECT_STREQ(line.text(), "");
  EXPECT_EQ(line.feed(ch('k', KeyMod::Alt)), CommandLine::Action::Edited);
  EXPECT_STREQ(line.text(), "goto 50");
  EXPECT_EQ(line.feed(ch('k', KeyMod::Alt)), CommandLine::Action::None);  // already showing it
  line.setText("x");
  EXPECT_EQ(line.feed(special(SpecialKey::Up)), CommandLine::Action::Edited);
  EXPECT_STREQ(line.text(), "goto 50");
}

TEST(CommandLineEdit, BlankLinesAreNotRemembered) {
  CommandLine line;
  line.open("toc");
  line.remember();
  line.open("   ");
  line.remember();
  EXPECT_STREQ(line.lastCommand(), "toc");
}

TEST(CommandLineEdit, IgnoredKeys) {
  CommandLine line;
  line.open("a");
  EXPECT_EQ(line.feed(ch('x', KeyMod::Alt)), CommandLine::Action::None);
  EXPECT_EQ(line.feed(ch('x', KeyMod::Ctrl)), CommandLine::Action::None);
  EXPECT_EQ(line.feed(special(SpecialKey::Left)), CommandLine::Action::None);
  EXPECT_EQ(line.feed(ch('\n')), CommandLine::Action::None);
  EXPECT_STREQ(line.text(), "a");
}

TEST(CommandLineEdit, LengthIsCapped) {
  CommandLine line;
  line.open();
  for (size_t i = 0; i < CommandLine::MAX_LEN; ++i) EXPECT_EQ(line.feed(ch('x')), CommandLine::Action::Edited);
  EXPECT_EQ(line.feed(ch('y')), CommandLine::Action::None);
  EXPECT_EQ(line.length(), CommandLine::MAX_LEN);
  std::string longText(200, 'z');
  line.setText(longText.c_str());
  EXPECT_EQ(line.length(), CommandLine::MAX_LEN);
}
