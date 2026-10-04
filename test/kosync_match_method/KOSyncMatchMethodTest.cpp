#include <gtest/gtest.h>

#include "KOReaderMatchMethod.h"

using kosync::loadMatchMethod;

TEST(KOSyncMatchMethod, OldFilenameConfigsMigrateToBinary) {
  EXPECT_EQ(loadMatchMethod(0, 1), DocumentMatchMethod::BINARY);
  EXPECT_EQ(loadMatchMethod(0, 2), DocumentMatchMethod::BINARY);
}

TEST(KOSyncMatchMethod, BinaryStaysBinary) {
  EXPECT_EQ(loadMatchMethod(1, 1), DocumentMatchMethod::BINARY);
  EXPECT_EQ(loadMatchMethod(1, 3), DocumentMatchMethod::BINARY);
}

TEST(KOSyncMatchMethod, ExplicitFilenameIsBinaryToo) {
  EXPECT_EQ(loadMatchMethod(0, kosync::MATCH_METHOD_BINARY_DEFAULT_VERSION), DocumentMatchMethod::BINARY);
}

TEST(KOSyncMatchMethod, InvalidValuesAreBinary) {
  EXPECT_EQ(loadMatchMethod(7, 3), DocumentMatchMethod::BINARY);
  EXPECT_EQ(loadMatchMethod(-1, 1), DocumentMatchMethod::BINARY);
}
