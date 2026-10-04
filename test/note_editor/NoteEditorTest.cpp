#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "deckpoint/NoteEditor.h"

using deckpoint::NoteEditor;
using deckpoint::placeNoteSheet;
using deckpoint::SheetPlacement;
using deckpoint::TextMeasure;
using Edit = NoteEditor::Edit;

namespace {

// Every code point is 10 px wide.
int tenPerCodepoint(void*, const char* text, const size_t bytes) {
  int n = 0;
  for (size_t i = 0; i < bytes; i++) {
    if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) n++;
  }
  return n * 10;
}

const TextMeasure MEASURE{nullptr, &tenPerCodepoint};

std::string lineText(const NoteEditor& ed, const size_t i) {
  const auto& l = ed.line(i);
  return std::string(ed.text().substr(l.start, l.end - l.start));
}

NoteEditor opened(std::string_view text, const size_t cap = 2048) {
  NoteEditor ed;
  EXPECT_TRUE(ed.open(text, cap));
  return ed;
}

}  // namespace

TEST(NoteEditor, OpenPutsCursorAtEndAndIsNotEdited) {
  NoteEditor ed = opened("hello");
  EXPECT_EQ(ed.text(), "hello");
  EXPECT_EQ(ed.cursor(), 5u);
  EXPECT_FALSE(ed.edited());
  ed.close();
  EXPECT_FALSE(ed.isOpen());
  EXPECT_EQ(ed.size(), 0u);
}

TEST(NoteEditor, InsertAndDeleteMultibyte) {
  NoteEditor ed = opened("");
  EXPECT_EQ(ed.insert("caf", 3), Edit::Changed);
  EXPECT_EQ(ed.insert("\xC3\xA9", 2), Edit::Changed);  // é
  EXPECT_EQ(ed.text(), "caf\xC3\xA9");
  EXPECT_TRUE(ed.edited());
  // Backspace removes the whole code point.
  EXPECT_EQ(ed.backspace(), Edit::Changed);
  EXPECT_EQ(ed.text(), "caf");
  EXPECT_EQ(ed.insert("\xE2\x82\xAC", 3), Edit::Changed);  // €
  EXPECT_TRUE(ed.left());
  EXPECT_EQ(ed.cursor(), 3u);
  EXPECT_TRUE(ed.left());
  EXPECT_EQ(ed.cursor(), 2u);
  EXPECT_EQ(ed.deleteForward(), Edit::Changed);  // 'f'
  EXPECT_EQ(ed.text(), "ca\xE2\x82\xAC");
  EXPECT_TRUE(ed.right());
  EXPECT_EQ(ed.cursor(), 5u);  // past the 3-byte euro
  EXPECT_FALSE(ed.right());
  EXPECT_EQ(ed.deleteForward(), Edit::Unchanged);
}

TEST(NoteEditor, InsertInMiddleAndBackspaceAtStart) {
  NoteEditor ed = opened("ac");
  ed.left();
  ed.insertChar('b');
  EXPECT_EQ(ed.text(), "abc");
  EXPECT_EQ(ed.cursor(), 2u);
  ed.left();
  ed.left();
  EXPECT_EQ(ed.backspace(), Edit::Unchanged);
  EXPECT_FALSE(ed.left());
}

TEST(NoteEditor, ClearEmptiesAndCountsAsEdit) {
  NoteEditor ed = opened("some note");
  EXPECT_EQ(ed.clear(), Edit::Changed);
  EXPECT_EQ(ed.text(), "");
  EXPECT_EQ(ed.cursor(), 0u);
  EXPECT_TRUE(ed.edited());
  EXPECT_EQ(ed.clear(), Edit::Unchanged);
}

TEST(NoteEditor, CapRefusesWholeInsert) {
  NoteEditor ed = opened("abc", 5);
  EXPECT_EQ(ed.cap(), 5u);
  EXPECT_EQ(ed.insertChar('d'), Edit::Changed);
  EXPECT_EQ(ed.insert("\xC3\xA9", 2), Edit::Full);  // would be 6
  EXPECT_EQ(ed.text(), "abcd");
  EXPECT_EQ(ed.insertChar('e'), Edit::Changed);
  EXPECT_EQ(ed.insertChar('f'), Edit::Full);
  EXPECT_EQ(ed.newline(), Edit::Full);
  EXPECT_EQ(ed.size(), 5u);
}

TEST(NoteEditor, LongerInitialTextRaisesCap) {
  NoteEditor ed = opened("0123456789", 4);
  EXPECT_EQ(ed.cap(), 10u);
  EXPECT_EQ(ed.insertChar('x'), Edit::Full);
  ed.backspace();
  EXPECT_EQ(ed.insertChar('x'), Edit::Changed);
}

TEST(NoteEditor, WrapsAtWordsWithHangingSpaces) {
  NoteEditor ed = opened("the quick brown fox");
  ed.layout(MEASURE, 100);  // 10 code points per line
  ASSERT_EQ(ed.lineCount(), 2u);
  EXPECT_EQ(lineText(ed, 0), "the quick ");
  EXPECT_EQ(lineText(ed, 1), "brown fox");
}

TEST(NoteEditor, BreaksWordsWiderThanTheLine) {
  NoteEditor ed = opened("ab abcdefghijkl x");
  ed.layout(MEASURE, 50);  // 5 per line
  ASSERT_EQ(ed.lineCount(), 4u);
  EXPECT_EQ(lineText(ed, 0), "ab ");
  EXPECT_EQ(lineText(ed, 1), "abcde");
  EXPECT_EQ(lineText(ed, 2), "fghij");
  EXPECT_EQ(lineText(ed, 3), "kl x");  // the broken word's tail shares its line
}

TEST(NoteEditor, BreaksMultibyteWordsOnCodepoints) {
  NoteEditor ed = opened("\xC3\xA9\xC3\xA9\xC3\xA9");  // ééé
  ed.layout(MEASURE, 20);
  ASSERT_EQ(ed.lineCount(), 2u);
  EXPECT_EQ(lineText(ed, 0), "\xC3\xA9\xC3\xA9");
  EXPECT_EQ(lineText(ed, 1), "\xC3\xA9");
}

TEST(NoteEditor, ExplicitNewlinesMakeLines) {
  NoteEditor ed = opened("one\n\ntwo\n");
  ed.layout(MEASURE, 100);
  ASSERT_EQ(ed.lineCount(), 4u);
  EXPECT_EQ(lineText(ed, 0), "one");
  EXPECT_TRUE(ed.line(0).newline);
  EXPECT_EQ(lineText(ed, 1), "");
  EXPECT_EQ(lineText(ed, 2), "two");
  EXPECT_EQ(lineText(ed, 3), "");  // after the trailing newline
  EXPECT_EQ(ed.cursorLine(), 3u);
}

TEST(NoteEditor, EmptyTextHasOneLine) {
  NoteEditor ed = opened("");
  ed.layout(MEASURE, 100);
  ASSERT_EQ(ed.lineCount(), 1u);
  EXPECT_EQ(ed.cursorLine(), 0u);
  EXPECT_EQ(ed.cursorX(MEASURE), 0);
}

TEST(NoteEditor, CursorOnSoftWrapShowsOnNextLine) {
  NoteEditor ed = opened("the quick brown");
  ed.layout(MEASURE, 100);
  ASSERT_EQ(ed.lineCount(), 2u);
  while (ed.cursor() > 10) ed.left();  // start of "brown"
  EXPECT_EQ(ed.cursorLine(), 1u);
  EXPECT_EQ(ed.cursorX(MEASURE), 0);
  ed.left();  // the hanging space
  EXPECT_EQ(ed.cursorLine(), 0u);
  EXPECT_EQ(ed.cursorX(MEASURE), 90);
}

TEST(NoteEditor, EachWordThatDoesNotFitStartsALine) {
  NoteEditor ed = opened("abcdefgh ij klmnopqr");
  ed.layout(MEASURE, 100);
  ASSERT_EQ(ed.lineCount(), 3u);
  EXPECT_EQ(lineText(ed, 0), "abcdefgh ");
  EXPECT_EQ(lineText(ed, 1), "ij ");  // "klmnopqr" (80) does not fit after it (30)
  EXPECT_EQ(lineText(ed, 2), "klmnopqr");
}

TEST(NoteEditor, UpDownMovesByVisualLine) {
  NoteEditor ed = opened("aaaa bbbb\ncc\ndddddddd");
  ed.layout(MEASURE, 100);
  ASSERT_EQ(ed.lineCount(), 3u);
  // Cursor at the end of "dddddddd" (x = 80).
  EXPECT_EQ(ed.cursorLine(), 2u);
  EXPECT_TRUE(ed.up(MEASURE));
  // "cc" is shorter: end of that line, before its newline.
  EXPECT_EQ(ed.cursorLine(), 1u);
  EXPECT_EQ(ed.cursor(), 12u);
  EXPECT_TRUE(ed.up(MEASURE));
  EXPECT_EQ(ed.cursorLine(), 0u);
  EXPECT_EQ(ed.cursor(), 2u);  // x = 20
  EXPECT_FALSE(ed.up(MEASURE));
  EXPECT_TRUE(ed.down(MEASURE));
  EXPECT_TRUE(ed.down(MEASURE));
  EXPECT_EQ(ed.cursorLine(), 2u);
  EXPECT_EQ(ed.cursor(), 15u);  // "dd|dddddd"
  EXPECT_FALSE(ed.down(MEASURE));
}

TEST(NoteEditor, DownOntoSoftWrappedLineStaysOnIt) {
  NoteEditor ed = opened("x\nthe quick brown");
  ed.layout(MEASURE, 100);
  ASSERT_EQ(ed.lineCount(), 3u);
  while (ed.cursor() > 1) ed.left();  // after "x"
  // Far right on a soft-wrapped line lands on its hanging space, not the next line.
  ed.moveTo(1, 500, MEASURE);
  EXPECT_EQ(ed.cursorLine(), 1u);
  EXPECT_EQ(ed.cursor(), 11u);  // before the hanging space after "quick"
}

TEST(NoteEditor, HomeEndOnVisualLine) {
  NoteEditor ed = opened("one two\nthree");
  ed.layout(MEASURE, 100);
  EXPECT_TRUE(ed.home());
  EXPECT_EQ(ed.cursor(), 8u);
  EXPECT_TRUE(ed.up(MEASURE));
  EXPECT_TRUE(ed.end());
  EXPECT_EQ(ed.cursor(), 7u);  // before the newline
}

TEST(NoteEditor, MoveToPicksNearestPosition) {
  NoteEditor ed = opened("abcdef");
  ed.layout(MEASURE, 100);
  ed.moveTo(0, 24, MEASURE);
  EXPECT_EQ(ed.cursor(), 2u);
  ed.moveTo(0, 26, MEASURE);
  EXPECT_EQ(ed.cursor(), 3u);
  ed.moveTo(0, -5, MEASURE);
  EXPECT_EQ(ed.cursor(), 0u);
  ed.moveTo(7, 1000, MEASURE);  // past the last line: clamped
  EXPECT_EQ(ed.cursor(), 6u);
}

TEST(NoteEditor, ScrollKeepsCursorVisible) {
  NoteEditor ed = opened("1\n2\n3\n4\n5\n6");
  ed.layout(MEASURE, 100);
  ASSERT_EQ(ed.lineCount(), 6u);
  ed.scrollToCursor(3);
  EXPECT_EQ(ed.scrollTop(), 3u);  // cursor on line 5: lines 3-5 shown
  for (int i = 0; i < 4; i++) ed.up(MEASURE);
  ed.scrollToCursor(3);
  EXPECT_EQ(ed.cursorLine(), 1u);
  EXPECT_EQ(ed.scrollTop(), 1u);
  ed.up(MEASURE);
  ed.scrollToCursor(3);
  EXPECT_EQ(ed.scrollTop(), 0u);
  // Deleting lines pulls the window back up (no blank rows below the text).
  ed.down(MEASURE);
  ed.down(MEASURE);
  ed.down(MEASURE);
  ed.down(MEASURE);
  ed.scrollToCursor(3);
  EXPECT_EQ(ed.scrollTop(), 2u);
  ed.clear();
  ed.insert("a\nb", 3);
  ed.layout(MEASURE, 100);
  ed.scrollToCursor(3);
  EXPECT_EQ(ed.scrollTop(), 0u);
}

TEST(NoteEditor, TypingNewlineThenWrapUpdatesLines) {
  NoteEditor ed = opened("");
  for (const char c : std::string("hello world")) ed.insertChar(c);
  ed.layout(MEASURE, 100);
  EXPECT_EQ(ed.lineCount(), 2u);
  ed.newline();
  ed.layout(MEASURE, 100);
  EXPECT_EQ(ed.lineCount(), 3u);
  EXPECT_EQ(ed.cursorLine(), 2u);
}

TEST(NoteSheet, BottomWhenThePassageIsAbove) {
  const SheetPlacement p = placeNoteSheet(320, 16, 40, 80, 128);
  EXPECT_FALSE(p.atTop);
  EXPECT_EQ(p.y, 320 - 16 - 128);
  EXPECT_EQ(p.h, 128);
}

TEST(NoteSheet, TopWhenThePassageSitsLow) {
  const SheetPlacement p = placeNoteSheet(320, 16, 220, 260, 128);
  EXPECT_TRUE(p.atTop);
  EXPECT_EQ(p.y, 0);
}

TEST(NoteSheet, TallPassageCoveredLessWins) {
  // Bottom sheet starts at 176: covers 176..300 (124 px). Top covers 100..128 (28 px).
  EXPECT_TRUE(placeNoteSheet(320, 16, 100, 300, 128).atTop);
  // Bottom covers 176..200 (24 px); top covers 20..128 (108 px).
  EXPECT_FALSE(placeNoteSheet(320, 16, 20, 200, 128).atTop);
}
