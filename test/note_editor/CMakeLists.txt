# DECKPOINT: note editor model (UTF-8 editing, wrapping, cursor, scroll, cap, sheet placement).
add_executable(NoteEditorTest
  NoteEditorTest.cpp
  ${REPO_ROOT}/src/deckpoint/NoteEditor.cpp
)

target_include_directories(NoteEditorTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(NoteEditorTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(NoteEditorTest)
