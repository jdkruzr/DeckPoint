# DECKPOINT: sticky modifier state machine (BoardTDeckPro) and the badge debounce/text.
add_executable(ModifierBadgeTest
  ModifierBadgeTest.cpp
  ${REPO_ROOT}/src/deckpoint/ModifierBadge.cpp
)

target_include_directories(ModifierBadgeTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/hal
  ${REPO_ROOT}/freeink-sdk/libs/hardware/BoardTDeckPro/src
)

target_link_libraries(ModifierBadgeTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ModifierBadgeTest)
