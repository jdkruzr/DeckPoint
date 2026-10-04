#include <gtest/gtest.h>

#include <cstring>

#include "deckpoint/ota/OtaRelease.h"

using deckpoint_ota::firmwareAssetName;
using deckpoint_ota::isNewerRelease;
using deckpoint_ota::parseVersion;
using deckpoint_ota::Version;

TEST(OtaRelease, ParsesTagsAndBuildVersions) {
  Version v;
  ASSERT_TRUE(parseVersion("v0.1.0", v));
  EXPECT_EQ(v.major, 0);
  EXPECT_EQ(v.minor, 1);
  EXPECT_EQ(v.patch, 0);
  EXPECT_FALSE(v.preRelease);

  ASSERT_TRUE(parseVersion("1.12.30-tdeckpro", v));
  EXPECT_EQ(v.minor, 12);
  EXPECT_EQ(v.patch, 30);
  EXPECT_FALSE(v.preRelease);

  ASSERT_TRUE(parseVersion("0.2.0-rc+abc1234", v));
  EXPECT_TRUE(v.preRelease);
  ASSERT_TRUE(parseVersion("0.2.0-dev-deckpoint-abc1234", v));
  EXPECT_TRUE(v.preRelease);
  ASSERT_TRUE(parseVersion("0.2.0-tdeckpro-rc+abc1234", v));
  EXPECT_TRUE(v.preRelease);
}

TEST(OtaRelease, RejectsMalformedVersions) {
  Version v;
  EXPECT_FALSE(parseVersion(nullptr, v));
  EXPECT_FALSE(parseVersion("", v));
  EXPECT_FALSE(parseVersion("latest", v));
  EXPECT_FALSE(parseVersion("v1.2", v));
  EXPECT_FALSE(parseVersion("1.2.x", v));
  EXPECT_FALSE(parseVersion("1.2.3beta", v));
  EXPECT_FALSE(parseVersion("99999999999.0.0", v));
}

TEST(OtaRelease, OrdersByNumericTriple) {
  EXPECT_TRUE(isNewerRelease("v0.1.1", "0.1.0-tdeckpro"));
  EXPECT_TRUE(isNewerRelease("v0.2.0", "0.1.9-tdeckpro"));
  EXPECT_TRUE(isNewerRelease("v1.0.0", "0.9.9-tdeckpro"));
  EXPECT_TRUE(isNewerRelease("v0.10.0", "0.9.0-tdeckpro"));  // numeric, not lexical
  EXPECT_FALSE(isNewerRelease("v0.1.0", "0.1.0-tdeckpro"));
  EXPECT_FALSE(isNewerRelease("v0.1.0", "0.1.1-tdeckpro"));
  EXPECT_FALSE(isNewerRelease("v0.1.0", "1.0.0"));
}

TEST(OtaRelease, PreReleasesOrderBelowTheirRelease) {
  EXPECT_TRUE(isNewerRelease("v0.2.0", "0.2.0-rc+abc1234"));
  EXPECT_TRUE(isNewerRelease("v0.2.0", "0.2.0-dev-deckpoint-abc1234"));
  // A pre-release tag never replaces anything (releases/latest skips them anyway).
  EXPECT_FALSE(isNewerRelease("v0.3.0-rc1", "0.2.0-tdeckpro"));
}

TEST(OtaRelease, UnparseableVersionsNeverUpdate) {
  EXPECT_FALSE(isNewerRelease("nightly", "0.1.0-tdeckpro"));
  EXPECT_FALSE(isNewerRelease("v0.2.0", "unknown"));
  EXPECT_FALSE(isNewerRelease(nullptr, "0.1.0"));
}

TEST(OtaRelease, AssetNameFollowsBoardTag) {
  char name[48];
  // Board names arrive as unterminated slices of the board tag.
  const char tag[] = "tdeckpro;";
  ASSERT_TRUE(firmwareAssetName(tag, 8, name, sizeof(name)));
  EXPECT_STREQ(name, "deckpoint-tdeckpro.bin");
  ASSERT_TRUE(firmwareAssetName("x4pro", 5, name, sizeof(name)));
  EXPECT_STREQ(name, "deckpoint-x4pro.bin");
}

TEST(OtaRelease, AssetNameRejectsEmptyAndOverflow) {
  char name[48];
  EXPECT_FALSE(firmwareAssetName("", 0, name, sizeof(name)));
  EXPECT_FALSE(firmwareAssetName(nullptr, 3, name, sizeof(name)));
  char tiny[12];
  EXPECT_FALSE(firmwareAssetName("tdeckpro", 8, tiny, sizeof(tiny)));
}
