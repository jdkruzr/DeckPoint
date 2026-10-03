add_executable(KeyHelpTextTest
  KeyHelpTextTest.cpp
  ${REPO_ROOT}/src/deckpoint/KeyHelpText.cpp
)

target_include_directories(KeyHelpTextTest PRIVATE ../../src)

target_link_libraries(KeyHelpTextTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(KeyHelpTextTest)
