add_executable(CommandsTest
  CommandsTest.cpp
  ${REPO_ROOT}/src/deckpoint/Commands.cpp
  ${REPO_ROOT}/src/deckpoint/CommandLine.cpp
)

target_include_directories(CommandsTest PRIVATE
  ../../src
  ${REPO_ROOT}/lib/I18n
  ${REPO_ROOT}/freeink-sdk/libs/hardware/KeyMatrix/include
)

target_link_libraries(CommandsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(CommandsTest)
