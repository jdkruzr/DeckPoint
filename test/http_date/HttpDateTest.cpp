// DECKPOINT: RFC 7231 IMF-fixdate parsing for trustedtime::applyHttpDate.

#include <gtest/gtest.h>

#include "TrustedTime/HttpDate.h"

using trustedtime::parseHttpDate;

TEST(HttpDate, ParsesRfcExample) {
  int64_t t = 0;
  ASSERT_TRUE(parseHttpDate("Sun, 06 Nov 1994 08:49:37 GMT", t));
  EXPECT_EQ(t, 784111777);
}

TEST(HttpDate, ParsesPresentDayAndLeapDay) {
  int64_t t = 0;
  ASSERT_TRUE(parseHttpDate("Sun, 04 Oct 2026 12:00:00 GMT", t));
  EXPECT_EQ(t, 1791115200);
  ASSERT_TRUE(parseHttpDate("Thu, 29 Feb 2024 23:59:59 GMT", t));
  EXPECT_EQ(t, 1709251199);
  ASSERT_TRUE(parseHttpDate("Thu, 01 Jan 1970 00:00:00 GMT", t));
  EXPECT_EQ(t, 0);
}

TEST(HttpDate, ToleratesSurroundingBlanks) {
  int64_t t = 0;
  EXPECT_TRUE(parseHttpDate("  Sun, 06 Nov 1994 08:49:37 GMT\r\n", t));
  EXPECT_EQ(t, 784111777);
}

TEST(HttpDate, LeapSecondReadsAsFiftyNine) {
  int64_t t = 0;
  ASSERT_TRUE(parseHttpDate("Sat, 31 Dec 2016 23:59:60 GMT", t));
  EXPECT_EQ(t, 1483228799);
}

TEST(HttpDate, RejectsMalformedAndObsoleteForms) {
  int64_t t = 42;
  const char* bad[] = {
      nullptr,
      "",
      "Sunday, 06-Nov-94 08:49:37 GMT",  // RFC 850
      "Sun Nov  6 08:49:37 1994",        // asctime
      "Sun, 6 Nov 1994 08:49:37 GMT",    // one-digit day
      "Sun, 06 Nov 1994 08:49:37 UTC",
      "Sun, 06 Nov 1994 08:49:37",
      "Sun, 06 Nov 1994 08:49:37 GMT x",
      "Xyz, 06 Nov 1994 08:49:37 GMT",
      "Sun, 06 Foo 1994 08:49:37 GMT",
      "Sun, 31 Apr 2026 08:49:37 GMT",
      "Sun, 29 Feb 2025 08:49:37 GMT",
      "Sun, 00 Nov 1994 08:49:37 GMT",
      "Sun, 06 Nov 1994 24:00:00 GMT",
      "Sun, 06 Nov 1994 08:60:00 GMT",
      "Sun, 06 Nov 1969 08:49:37 GMT",
      "Sun, 06 Nov 2100 08:49:37 GMT",
      "Sun, 06 Nov 19a4 08:49:37 GMT",
  };
  for (const char* s : bad) EXPECT_FALSE(parseHttpDate(s, t)) << (s ? s : "(null)");
  EXPECT_EQ(t, 42);
}
