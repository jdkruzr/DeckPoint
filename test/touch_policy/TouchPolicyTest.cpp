// DECKPOINT: touch thresholds, per-board gesture mask and stroke end point.

#include <gtest/gtest.h>

#include "TouchThresholds.h"
#include "deckpoint/TouchGestures.h"
#include "deckpoint/TouchStroke.h"

using namespace freeink;
using namespace deckpoint;
using namespace deckpoint::touch;

namespace {

void expectConsistent(const TouchThresholds& t) {
  EXPECT_LE(t.tapSlopPx, t.tapReleaseSlopPx);
  EXPECT_LT(t.tapReleaseSlopPx, t.swipeMinPx);
  EXPECT_GT(t.tapSlopPx, 0);
}

}  // namespace

TEST(TouchThresholds, ZeroFieldsKeepSdkDefaults) {
  constexpr TouchThresholds t = resolveTouchThresholds(0, 0, 0);
  EXPECT_EQ(t.swipeMinPx, 60);
  EXPECT_EQ(t.tapSlopPx, 28);
  EXPECT_EQ(t.tapReleaseSlopPx, 59);
  EXPECT_EQ(t.swipeMinPx, DEFAULT_TOUCH_THRESHOLDS.swipeMinPx);
  EXPECT_EQ(t.tapSlopPx, DEFAULT_TOUCH_THRESHOLDS.tapSlopPx);
  EXPECT_EQ(t.tapReleaseSlopPx, DEFAULT_TOUCH_THRESHOLDS.tapReleaseSlopPx);
}

TEST(TouchThresholds, TDeckProfile) {
  // BoardConfig TDECK_PRO_TOUCH: swipe 30, tap slop 16, release slop derived.
  constexpr TouchThresholds t = resolveTouchThresholds(30, 16, 0);
  EXPECT_EQ(t.swipeMinPx, 30);
  EXPECT_EQ(t.tapSlopPx, 16);
  EXPECT_EQ(t.tapReleaseSlopPx, 29);
  expectConsistent(t);
}

TEST(TouchThresholds, RoundOneSwipesNowClassify) {
  // Calibration round 1 natural swipes were 46-65 px; all clear the T-Deck rule.
  constexpr TouchThresholds t = resolveTouchThresholds(30, 16, 0);
  for (const int travel : {46, 52, 58, 65}) EXPECT_GE(travel, t.swipeMinPx) << travel;
  // ...and none of them can still be a tap on release.
  for (const int travel : {46, 52, 58, 65}) EXPECT_GT(travel, t.tapReleaseSlopPx) << travel;
}

TEST(TouchThresholds, ReleaseSlopClampedBelowSwipe) {
  // A release slop at/above the swipe distance would make one lift both a tap and a swipe.
  constexpr TouchThresholds t = resolveTouchThresholds(30, 16, 32);
  EXPECT_EQ(t.tapReleaseSlopPx, 29);
  expectConsistent(t);
}

TEST(TouchThresholds, TapSlopClampedToReleaseSlop) {
  constexpr TouchThresholds t = resolveTouchThresholds(20, 40, 0);
  EXPECT_EQ(t.tapReleaseSlopPx, 19);
  EXPECT_EQ(t.tapSlopPx, 19);
  expectConsistent(t);
}

TEST(TouchThresholds, DegenerateSwipeFallsBackToDefault) {
  constexpr TouchThresholds t = resolveTouchThresholds(1, 0, 0);
  EXPECT_EQ(t.swipeMinPx, 60);
  expectConsistent(t);
}

TEST(TouchGestures, TouchFirstBoardsKeepEveryGesture) {
  constexpr uint8_t mask = gestureMaskFor(false);
  for (const Gesture g : {GESTURE_READER_MENU_SWIPE, GESTURE_EDGE_BACK, GESTURE_EDGE_HOME, GESTURE_HEADER_BACK_TAP,
                          GESTURE_STATUS_BAR_TAP, GESTURE_LIGHT_PANEL_SWIPE}) {
    EXPECT_TRUE(gestureEnabled(mask, g)) << static_cast<int>(g);
  }
}

TEST(TouchGestures, KeyboardHybridKeepsOnlyReaderMenuSwipe) {
  constexpr uint8_t mask = gestureMaskFor(true);
  EXPECT_TRUE(gestureEnabled(mask, GESTURE_READER_MENU_SWIPE));
  for (const Gesture g : {GESTURE_EDGE_BACK, GESTURE_EDGE_HOME, GESTURE_HEADER_BACK_TAP, GESTURE_STATUS_BAR_TAP,
                          GESTURE_LIGHT_PANEL_SWIPE}) {
    EXPECT_FALSE(gestureEnabled(mask, g)) << static_cast<int>(g);
  }
}

TEST(TouchStroke, EndPointIsFinalSampleNotLastLogged) {
  TouchStroke s;
  s.begin(100, 200);
  s.track(110, 200);  // logged
  s.track(121, 201);  // inside a log throttle window: not logged, still tracked
  ASSERT_TRUE(s.end(124, 201));
  EXPECT_EQ(s.lastX, 124);
  EXPECT_EQ(s.lastY, 201);
  EXPECT_EQ(s.dx(), 24);
  EXPECT_EQ(s.dy(), 1);
  EXPECT_FALSE(s.active);
}

TEST(TouchStroke, StationaryLiftHasZeroTravel) {
  TouchStroke s;
  s.begin(50, 60);
  ASSERT_TRUE(s.end(50, 60));
  EXPECT_EQ(s.dx(), 0);
  EXPECT_EQ(s.dy(), 0);
}

TEST(TouchStroke, LiftWithoutDownIsIgnored) {
  TouchStroke s;
  s.track(10, 10);
  EXPECT_FALSE(s.end(20, 20));
  EXPECT_EQ(s.lastX, 0);
}
