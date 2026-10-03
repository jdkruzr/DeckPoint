add_executable(ReaderKeysTest
  ReaderKeysTest.cpp
  ${REPO_ROOT}/src/deckpoint/reader/ReaderKeys.cpp
  ${REPO_ROOT}/src/deckpoint/reader/Marks.cpp
)

target_include_directories(ReaderKeysTest PRIVATE
  ../../src
  ${REPO_ROOT}/freeink-sdk/libs/hardware/KeyMatrix/include
)

target_link_libraries(ReaderKeysTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ReaderKeysTest)
