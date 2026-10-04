# DECKPOINT: annotation sync decisions (GET/PUT outcomes, retry) and the result summary.
add_executable(AnnotationSyncPlanTest
  AnnotationSyncPlanTest.cpp
  ${REPO_ROOT}/src/deckpoint/sync/AnnotationSyncPlan.cpp
)

target_include_directories(AnnotationSyncPlanTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/src/deckpoint/sync
)

target_link_libraries(AnnotationSyncPlanTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(AnnotationSyncPlanTest)
