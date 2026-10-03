add_executable(SearchMatchTest
  SearchMatchTest.cpp
  ${REPO_ROOT}/src/deckpoint/reader/SearchMatch.cpp
)

target_include_directories(SearchMatchTest PRIVATE
  ../../src
)

target_link_libraries(SearchMatchTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SearchMatchTest)
