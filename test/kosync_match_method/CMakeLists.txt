# DECKPOINT: KOSync document matching default / Filename -> Binary migration.
add_executable(KOSyncMatchMethodTest KOSyncMatchMethodTest.cpp)

target_include_directories(KOSyncMatchMethodTest PRIVATE ${REPO_ROOT}/lib/KOReaderSync)

target_link_libraries(KOSyncMatchMethodTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(KOSyncMatchMethodTest)
