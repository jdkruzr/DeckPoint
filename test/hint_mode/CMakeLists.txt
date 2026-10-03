add_executable(HintModeTest
  HintModeTest.cpp
  ${REPO_ROOT}/src/deckpoint/reader/HintMode.cpp
  ${REPO_ROOT}/src/deckpoint/reader/WordToken.cpp
)

target_include_directories(HintModeTest PRIVATE
  ../../src
  ${REPO_ROOT}/freeink-sdk/libs/hardware/KeyMatrix/include
)

target_link_libraries(HintModeTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(HintModeTest)
