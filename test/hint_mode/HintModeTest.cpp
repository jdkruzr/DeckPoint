#include <gtest/gtest.h>

#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "deckpoint/reader/HintMode.h"
#include "deckpoint/reader/WordToken.h"

using deckpoint::reader::HintLabels;
using deckpoint::reader::HintMatcher;
using deckpoint::reader::isSelectableToken;
using freeink::KeyEvent;
using freeink::SpecialKey;
using Result = HintMatcher::Result;

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

std::vector<std::string> allLabels(uint16_t n) {
  HintLabels labels(n);
  std::vector<std::string> out;
  for (uint16_t i = 0; i < labels.count(); i++) {
    char buf[HintLabels::MAX_LABEL_LEN + 1];
    labels.label(i, buf);
    out.emplace_back(buf);
  }
  return out;
}

}  // namespace

TEST(HintLabels, AlphabetIsHomeRowFirstAndHasEveryLetterOnce) {
  EXPECT_EQ(HintLabels::ALPHABET_SIZE, 26);
  EXPECT_EQ(std::string(HintLabels::ALPHABET, 8), "asdfjklg");
  std::set<char> letters(HintLabels::ALPHABET, HintLabels::ALPHABET + HintLabels::ALPHABET_SIZE);
  EXPECT_EQ(letters.size(), 26u);
  for (char c = 'a'; c <= 'z'; c++) EXPECT_EQ(letters.count(c), 1u) << c;
}

TEST(HintLabels, SingleLettersWhenTheyFit) {
  const auto labels = allLabels(26);
  ASSERT_EQ(labels.size(), 26u);
  for (size_t i = 0; i < labels.size(); i++) {
    EXPECT_EQ(labels[i], std::string(1, HintLabels::ALPHABET[i]));
  }
  EXPECT_EQ(allLabels(3), (std::vector<std::string>{"a", "s", "d"}));
  EXPECT_TRUE(allLabels(0).empty());
}

TEST(HintLabels, UniquePrefixFreeAndShortForEveryCount) {
  for (uint16_t n = 1; n <= HintLabels::MAX_TARGETS; n++) {
    const auto labels = allLabels(n);
    ASSERT_EQ(labels.size(), n);
    std::set<std::string> seen;
    for (const auto& l : labels) {
      ASSERT_GE(l.size(), 1u);
      ASSERT_LE(l.size(), 2u) << "n=" << n;
      ASSERT_TRUE(seen.insert(l).second) << "duplicate " << l << " n=" << n;
    }
    for (const auto& a : labels) {
      if (a.size() != 1) continue;
      for (const auto& b : labels) {
        if (b.size() == 2) {
          ASSERT_NE(b[0], a[0]) << a << " prefixes " << b << " n=" << n;
        }
      }
    }
  }
}

TEST(HintLabels, ShortLabelsComeFirstInReadingOrder) {
  for (const uint16_t n : {27, 60, 100, 300, 676}) {
    const auto labels = allLabels(n);
    bool seenTwo = false;
    for (const auto& l : labels) {
      if (l.size() == 2) seenTwo = true;
      ASSERT_FALSE(seenTwo && l.size() == 1) << "single after a double, n=" << n;
    }
  }
}

TEST(HintLabels, KeepsAsManySingleLettersAsPossible) {
  // s singles leave 26 - s prefixes of 26 labels: s is the largest that fits.
  for (uint16_t n = 27; n <= HintLabels::MAX_TARGETS; n++) {
    const HintLabels labels(n);
    const int s = labels.singles();
    ASSERT_GE(s + (26 - s) * 26, n);
    ASSERT_LT((s + 1) + (26 - (s + 1)) * 26, n) << "n=" << n;
  }
  EXPECT_EQ(HintLabels(27).singles(), 25);
  EXPECT_EQ(HintLabels(676).singles(), 0);
  EXPECT_EQ(allLabels(27)[25], "ya");
  EXPECT_EQ(allLabels(27)[26], "ys");
}

TEST(HintLabels, ClampsAtTheLimit) {
  EXPECT_EQ(HintLabels(1000).count(), HintLabels::MAX_TARGETS);
  char buf[HintLabels::MAX_LABEL_LEN + 1];
  HintLabels(5).label(5, buf);
  EXPECT_STREQ(buf, "");
}

TEST(HintLabels, FindIsTheInverseOfLabel) {
  for (const uint16_t n : {1, 26, 27, 80, 676}) {
    const HintLabels labels(n);
    for (uint16_t i = 0; i < labels.count(); i++) {
      char buf[HintLabels::MAX_LABEL_LEN + 1];
      labels.label(i, buf);
      ASSERT_EQ(labels.find(buf), i) << buf;
    }
  }
  const HintLabels small(3);
  EXPECT_EQ(small.find("f"), -1);
  EXPECT_EQ(small.find("as"), -1);
  EXPECT_EQ(small.find(""), -1);
  EXPECT_EQ(small.find("A"), -1);
}

TEST(HintMatcher, SingleLetterSelects) {
  HintMatcher m;
  m.begin(10);
  EXPECT_EQ(m.feed(ch('d')), Result::Selected);
  EXPECT_EQ(m.selected(), 2);
}

TEST(HintMatcher, UppercaseCountsAsTheLetter) {
  HintMatcher m;
  m.begin(10);
  EXPECT_EQ(m.feed(ch('F')), Result::Selected);
  EXPECT_EQ(m.selected(), 3);
}

TEST(HintMatcher, UnusedLetterCancels) {
  HintMatcher m;
  m.begin(3);
  EXPECT_EQ(m.feed(ch('f')), Result::Cancelled);
  m.begin(3);
  EXPECT_EQ(m.feed(ch('1')), Result::Cancelled);
  m.begin(3);
  EXPECT_EQ(m.feed(ch('?')), Result::Cancelled);
}

TEST(HintMatcher, TwoLetterNarrowsThenSelects) {
  HintMatcher m;
  m.begin(100);
  const HintLabels& labels = m.labels();
  const int s = labels.singles();
  char last[HintLabels::MAX_LABEL_LEN + 1];
  labels.label(99, last);
  ASSERT_EQ(strlen(last), 2u);

  EXPECT_EQ(m.feed(ch(last[0])), Result::Narrowed);
  EXPECT_STREQ(m.typed(), std::string(1, last[0]).c_str());
  int matching = 0;
  for (uint16_t i = 0; i < labels.count(); i++) {
    if (m.matches(i)) {
      matching++;
      EXPECT_GE(i, s);
    }
  }
  EXPECT_GT(matching, 1);
  EXPECT_TRUE(m.matches(99));
  EXPECT_FALSE(m.matches(0));

  EXPECT_EQ(m.feed(ch(last[1])), Result::Selected);
  EXPECT_EQ(m.selected(), 99);
}

TEST(HintMatcher, SecondLetterPastTheLastTargetCancels) {
  HintMatcher m;
  m.begin(27);  // "ya", "ys" only
  EXPECT_EQ(m.feed(ch('y')), Result::Narrowed);
  EXPECT_EQ(m.feed(ch('d')), Result::Cancelled);
}

TEST(HintMatcher, BackspaceUndoesThenCancels) {
  HintMatcher m;
  m.begin(100);
  char label[HintLabels::MAX_LABEL_LEN + 1];
  m.labels().label(99, label);
  EXPECT_EQ(m.feed(ch(label[0])), Result::Narrowed);
  EXPECT_EQ(m.feed(special(SpecialKey::Backspace)), Result::Narrowed);
  EXPECT_STREQ(m.typed(), "");
  EXPECT_TRUE(m.matches(0));
  EXPECT_EQ(m.feed(special(SpecialKey::Backspace)), Result::Cancelled);
}

TEST(HintMatcher, EscCancelsOtherSpecialsIgnored) {
  HintMatcher m;
  m.begin(100);
  EXPECT_EQ(m.feed(special(SpecialKey::Enter)), Result::Ignored);
  EXPECT_EQ(m.feed(special(SpecialKey::Down)), Result::Ignored);
  KeyEvent shiftOnly;  // modifier press: no char, no special
  EXPECT_EQ(m.feed(shiftOnly), Result::Ignored);
  EXPECT_EQ(m.feed(special(SpecialKey::Escape)), Result::Cancelled);
}

TEST(WordToken, SelectableTokens) {
  EXPECT_TRUE(isSelectableToken("word"));
  EXPECT_TRUE(isSelectableToken("\"quoted,\""));
  EXPECT_TRUE(isSelectableToken("42"));
  EXPECT_TRUE(isSelectableToken("caf\xC3\xA9"));
  EXPECT_FALSE(isSelectableToken(""));
  EXPECT_FALSE(isSelectableToken("--"));
  EXPECT_FALSE(isSelectableToken("\xE2\x80\x94"));  // em dash
  EXPECT_FALSE(isSelectableToken("\xE2\x80\xA2"));  // bullet
  EXPECT_FALSE(isSelectableToken("\xE2\x80"));      // truncated sequence
}

// Tag placement: above the word (T-Deck numbers: Noto Serif compact reader
// font, ascender 17, line top at y; Ubuntu 11 px label font, ascender 11).
TEST(HintTag, SitsAboveTheWordsFirstLetter) {
  using deckpoint::reader::hintLabelCapHeight;
  using deckpoint::reader::hintTagRect;
  const int tagH = hintLabelCapHeight(11) + 2;
  EXPECT_EQ(hintLabelCapHeight(11), 9);
  EXPECT_LT(tagH, 13 + 2);  // smaller than the old full-line-box tag
  const auto tag = hintTagRect(40, 100, 17, 12, tagH, 240, 320);
  EXPECT_EQ(tag.x, 40);
  // Bottom edge at line top + ascender / 3 (5 px): above Noto's cap top
  // (~5.6 px) and well above its x-height (~8.4 px).
  EXPECT_EQ(tag.y + tag.height, 105);
  EXPECT_LE(tag.y + tag.height, 100 + 17 / 3);
  EXPECT_LT(tag.y, 100);  // the rest rises into the line gap
}

TEST(HintTag, ClampedOnScreen) {
  using deckpoint::reader::hintTagRect;
  const auto top = hintTagRect(-3, 2, 17, 12, 11, 240, 320);
  EXPECT_EQ(top.x, 0);
  EXPECT_EQ(top.y, 0);
  const auto right = hintTagRect(235, 100, 17, 12, 11, 240, 320);
  EXPECT_EQ(right.x, 240 - 12);
  const auto bottom = hintTagRect(10, 330, 17, 12, 11, 240, 320);
  EXPECT_EQ(bottom.y, 320 - 11);
}
