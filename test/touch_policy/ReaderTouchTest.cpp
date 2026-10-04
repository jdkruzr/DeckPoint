// DECKPOINT: reader tap zones, long-press word hit-test, and the T-Deck reader
// touch defaults + their one-time migration.

#include <gtest/gtest.h>

#include <cstdint>
#include <iterator>
#include <utility>

#include "deckpoint/TouchDefaults.h"
#include "deckpoint/reader/ReaderTapZones.h"
#include "deckpoint/reader/WordHitTest.h"

using namespace deckpoint;

namespace {

// T-Deck Pro portrait logical frame.
constexpr int W = 240;
constexpr int H = 320;

struct Turn {
  bool prev;
  bool next;
};

Turn tap(const int x, const int y, const bool menuTap = true, const bool nextTaps = true, const bool prevTaps = true,
         const bool inverted = false) {
  const auto t = reader::tapTurnAt(x, y, W, H, menuTap, nextTaps, prevTaps, inverted);
  return {t.prev, t.next};
}

}  // namespace

TEST(ReaderTapZones, CenterThirdIsMenu) {
  EXPECT_TRUE(reader::inMenuTapZone(W / 2, H / 2, W, H));
  EXPECT_TRUE(reader::inMenuTapZone(80, 107, W, H));
  EXPECT_FALSE(reader::inMenuTapZone(79, H / 2, W, H));
  EXPECT_FALSE(reader::inMenuTapZone(160, H / 2, W, H));
  EXPECT_FALSE(reader::inMenuTapZone(W / 2, 105, W, H));  // H / 3 floors to 106
  EXPECT_TRUE(reader::inMenuTapZone(W / 2, 213, W, H));
  EXPECT_FALSE(reader::inMenuTapZone(W / 2, 214, W, H));
  // The center tap turns nothing while the menu tap is on.
  const Turn t = tap(W / 2, H / 2);
  EXPECT_FALSE(t.prev);
  EXPECT_FALSE(t.next);
}

TEST(ReaderTapZones, LeftThirdPreviousRightTwoThirdsNext) {
  for (const int y : {5, H / 2 - 60, H / 2 + 60, H - 5}) {
    if (reader::inMenuTapZone(10, y, W, H)) continue;
    const Turn left = tap(10, y);
    EXPECT_TRUE(left.prev) << y;
    EXPECT_FALSE(left.next) << y;
    const Turn edge = tap(79, y);
    EXPECT_TRUE(edge.prev) << y;
  }
  // Right two-thirds: anywhere from x = W/3 outside the center menu box.
  for (const auto& p : {std::pair{80, 20}, std::pair{W / 2, 20}, std::pair{W / 2, H - 20}, std::pair{230, H / 2}}) {
    const Turn t = tap(p.first, p.second);
    EXPECT_TRUE(t.next) << p.first << "," << p.second;
    EXPECT_FALSE(t.prev) << p.first << "," << p.second;
  }
}

TEST(ReaderTapZones, MenuTapOffGivesCenterToNext) {
  const Turn t = tap(W / 2, H / 2, /*menuTap=*/false);
  EXPECT_TRUE(t.next);
  EXPECT_FALSE(t.prev);
}

TEST(ReaderTapZones, InvertedMirrorsTheSplit) {
  const Turn right = tap(230, 20, true, true, true, /*inverted=*/true);
  EXPECT_TRUE(right.prev);
  EXPECT_FALSE(right.next);
  const Turn left = tap(10, 20, true, true, true, /*inverted=*/true);
  EXPECT_TRUE(left.next);
}

TEST(ReaderTapZones, SoleTapDirectionGetsWholePage) {
  const Turn nextOnly = tap(10, 20, true, /*nextTaps=*/true, /*prevTaps=*/false);
  EXPECT_TRUE(nextOnly.next);
  EXPECT_FALSE(nextOnly.prev);
  const Turn prevOnly = tap(230, 20, true, /*nextTaps=*/false, /*prevTaps=*/true);
  EXPECT_TRUE(prevOnly.prev);
  EXPECT_FALSE(prevOnly.next);
}

namespace {

struct Box {
  int x;
  int y;
  int width;
};

constexpr int LINE_H = 20;
constexpr int SLOP = 8;
// Two lines: "Call me Ishmael" at y=10, "Some years" at y=30.
constexpr Box WORDS[] = {{10, 10, 30}, {46, 10, 20}, {72, 10, 60}, {10, 30, 40}, {56, 30, 45}};

int at(const int x, const int y) { return reader::findWordAt(WORDS, std::size(WORDS), x, y, LINE_H, SLOP); }

}  // namespace

TEST(WordHitTest, InsideABoxPicksIt) {
  EXPECT_EQ(at(10, 10), 0);
  EXPECT_EQ(at(39, 29), 0);
  EXPECT_EQ(at(50, 15), 1);
  EXPECT_EQ(at(100, 12), 2);
  EXPECT_EQ(at(20, 35), 3);
  EXPECT_EQ(at(60, 49), 4);
}

TEST(WordHitTest, GapsGoToTheNearestWord) {
  // Word gap between "Call" (ends x=40) and "me" (starts x=46).
  EXPECT_EQ(at(41, 15), 0);
  EXPECT_EQ(at(45, 15), 1);
  // Left margin, within slop.
  EXPECT_EQ(at(4, 15), 0);
}

TEST(WordHitTest, NothingWithinReach) {
  EXPECT_EQ(at(200, 15), -1);  // far right of the last word
  EXPECT_EQ(at(20, 80), -1);   // below the text
  EXPECT_EQ(at(139, 15), 2);   // 8 px past "Ishmael" (box ends at x=131): within slop
  EXPECT_EQ(at(140, 15), -1);
  EXPECT_EQ(reader::findWordAt(WORDS, 0, 20, 15, LINE_H, SLOP), -1);
}

namespace {

struct Fields {
  uint8_t touchReaderControls;
  uint8_t pageTurnGesture;
  uint8_t previousPageGesture;
  uint8_t showReaderMenu;
  touch::ReaderTouchSettings refs() {
    return {touchReaderControls, pageTurnGesture, previousPageGesture, showReaderMenu};
  }
};

// CrossPoint's struct defaults: touch on, swipe-only both ways, menu tap.
constexpr Fields CROSSPOINT_DEFAULTS{touch::READER_TOUCH_ON, touch::GESTURE_SWIPE_ONLY, touch::GESTURE_SWIPE_ONLY,
                                     touch::MENU_TAP};

void expectHybrid(const Fields& f) {
  EXPECT_EQ(f.touchReaderControls, touch::READER_TOUCH_ON);
  EXPECT_EQ(f.pageTurnGesture, touch::GESTURE_TAP_AND_SWIPE);
  EXPECT_EQ(f.previousPageGesture, touch::GESTURE_TAP_AND_SWIPE);
  EXPECT_EQ(f.showReaderMenu, touch::MENU_TAP);
}

void expectUnchanged(const Fields& f, const Fields& was) {
  EXPECT_EQ(f.touchReaderControls, was.touchReaderControls);
  EXPECT_EQ(f.pageTurnGesture, was.pageTurnGesture);
  EXPECT_EQ(f.previousPageGesture, was.previousPageGesture);
  EXPECT_EQ(f.showReaderMenu, was.showReaderMenu);
}

}  // namespace

TEST(ReaderTouchDefaults, HybridBoardSeedsTapAndSwipe) {
  Fields f = CROSSPOINT_DEFAULTS;
  touch::applyReaderTouchDefaults(true, f.refs());
  expectHybrid(f);
}

TEST(ReaderTouchDefaults, TouchFirstBoardKeepsCrossPointDefaults) {
  Fields f = CROSSPOINT_DEFAULTS;
  touch::applyReaderTouchDefaults(false, f.refs());
  expectUnchanged(f, CROSSPOINT_DEFAULTS);
}

TEST(ReaderTouchDefaults, OldHybridFileMigratesOnce) {
  // A T-Deck file from before Phase T stored the SWIPE_ONLY defaults and no
  // revision key (stored revision reads as 0).
  Fields f = CROSSPOINT_DEFAULTS;
  EXPECT_TRUE(touch::migrateReaderTouchSettings(0, true, f.refs()));
  expectHybrid(f);
}

TEST(ReaderTouchDefaults, StampedFileKeepsUserChoices) {
  // After the resave the file carries the revision; a later SWIPE_ONLY is the
  // user's own choice and survives every load.
  Fields f = CROSSPOINT_DEFAULTS;
  f.showReaderMenu = 0;
  const Fields was = f;
  EXPECT_FALSE(touch::migrateReaderTouchSettings(touch::SETTINGS_REVISION, true, f.refs()));
  expectUnchanged(f, was);
}

TEST(ReaderTouchDefaults, OtherBoardsNeverMigrate) {
  Fields f = CROSSPOINT_DEFAULTS;
  EXPECT_FALSE(touch::migrateReaderTouchSettings(0, false, f.refs()));
  expectUnchanged(f, CROSSPOINT_DEFAULTS);
}
