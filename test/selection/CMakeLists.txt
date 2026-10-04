# DECKPOINT: highlight selection logic (anchor/head, ranges, text, popup menu, layout).
add_executable(SelectionTest
  SelectionTest.cpp
  ${REPO_ROOT}/src/deckpoint/reader/SelectionSession.cpp
  ${REPO_ROOT}/src/deckpoint/annotations/AnnotationGeometry.cpp
)

target_include_directories(SelectionTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/freeink-sdk/libs/hardware/KeyMatrix/include
)

target_link_libraries(SelectionTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SelectionTest)
