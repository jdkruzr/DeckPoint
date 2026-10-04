#include <gtest/gtest.h>

#include "KeyboardModifiers.h"
#include "StickyModifier.h"
#include "deckpoint/ModifierBadge.h"

using BoardTDeckPro::StickyModifier;
using deckpoint::ModifierDebounce;

namespace {
void tap(StickyModifier& m) {
  m.press();
  m.release();
}
}  // namespace

TEST(StickyModifier, TapLatchesDoubleTapLocksThirdTapOff) {
  StickyModifier shift;
  tap(shift);
  EXPECT_TRUE(shift.latched);
  EXPECT_FALSE(shift.locked);
  tap(shift);
  EXPECT_FALSE(shift.latched);
  EXPECT_TRUE(shift.locked);
  shift.consume();  // caps lock survives keys
  EXPECT_TRUE(shift.locked);
  tap(shift);
  EXPECT_FALSE(shift.active());
}

TEST(StickyModifier, AltNeverLocks) {
  StickyModifier alt{false};
  tap(alt);
  EXPECT_TRUE(alt.latched);
  tap(alt);  // second tap cancels instead of locking
  EXPECT_FALSE(alt.latched);
  EXPECT_FALSE(alt.locked);
  EXPECT_FALSE(alt.active());
  tap(alt);
  EXPECT_TRUE(alt.latched);
  alt.consume();  // one-shot spent by the next key
  EXPECT_FALSE(alt.active());
  for (int i = 0; i < 5; i++) {
    tap(alt);
    EXPECT_FALSE(alt.locked);
  }
}

TEST(StickyModifier, ChordDoesNotLatch) {
  StickyModifier alt{false};
  alt.press();
  EXPECT_TRUE(alt.active());
  EXPECT_FALSE(alt.sticky());
  alt.consume();  // key pressed while held
  alt.release();
  EXPECT_FALSE(alt.active());
}

TEST(ModifierDebounce, AdditionsWaitRemovalsAreImmediate) {
  ModifierDebounce d;
  EXPECT_FALSE(d.update(keymods::SYM, 1000));
  EXPECT_FALSE(d.update(keymods::SYM, 1000 + ModifierDebounce::SETTLE_MS - 1));
  EXPECT_TRUE(d.update(keymods::SYM, 1000 + ModifierDebounce::SETTLE_MS));
  EXPECT_EQ(d.shown(), keymods::SYM);
  EXPECT_TRUE(d.update(0, 1400));  // consumed: gone at once
  EXPECT_EQ(d.shown(), 0);
}

TEST(ModifierDebounce, QuickOneShotNeverShows) {
  ModifierDebounce d;
  EXPECT_FALSE(d.update(keymods::SYM, 0));
  EXPECT_FALSE(d.update(keymods::SYM, 100));
  EXPECT_FALSE(d.update(0, 150));  // Sym+':' typed
  EXPECT_FALSE(d.update(0, 1000));
  EXPECT_EQ(d.shown(), 0);
}

TEST(ModifierDebounce, LatchToLockRestartsSettle) {
  ModifierDebounce d;
  d.update(keymods::SHIFT, 0);
  EXPECT_TRUE(d.update(keymods::SHIFT, 400));
  EXPECT_FALSE(d.update(keymods::SHIFT_LOCK, 500));
  EXPECT_FALSE(d.update(keymods::SHIFT_LOCK, 700));
  EXPECT_TRUE(d.update(keymods::SHIFT_LOCK, 800));
  EXPECT_EQ(d.shown(), keymods::SHIFT_LOCK);
}

TEST(ModifierBadgeText, JoinsLabelsInOrder) {
  const deckpoint::ModifierNames names{{"SHIFT", "CAPS", "SYM", "SYM LOCK", "ALT"}};
  char out[32];
  EXPECT_EQ(deckpoint::formatModifierBadge(0, names, out, sizeof(out)), 0u);
  EXPECT_STREQ(out, "");
  deckpoint::formatModifierBadge(keymods::SHIFT_LOCK | keymods::ALT, names, out, sizeof(out));
  EXPECT_STREQ(out, "CAPS ALT");
  deckpoint::formatModifierBadge(keymods::SYM_LOCK, names, out, sizeof(out));
  EXPECT_STREQ(out, "SYM LOCK");
  char tiny[8];
  deckpoint::formatModifierBadge(keymods::SHIFT | keymods::SYM, names, tiny, sizeof(tiny));
  EXPECT_STREQ(tiny, "SHIFT");  // the next label does not fit
}
