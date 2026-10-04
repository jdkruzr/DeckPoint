# DECKPOINT: HTTP Date header parsing (TrustedTime's opportunistic clock source).
add_executable(HttpDateTest
  HttpDateTest.cpp
  ${REPO_ROOT}/lib/TrustedTime/HttpDate.cpp
)

target_link_libraries(HttpDateTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(HttpDateTest)
