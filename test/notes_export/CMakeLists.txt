# DECKPOINT: notes list order and `:export` (Markdown, My Clippings, clippings merge, file names).
add_executable(NotesExportTest
  NotesExportTest.cpp
  ${REPO_ROOT}/src/deckpoint/annotations/Annotation.cpp
  ${REPO_ROOT}/src/deckpoint/annotations/AnnotationExport.cpp
  ${REPO_ROOT}/src/deckpoint/annotations/AnnotationGeometry.cpp
  ${REPO_ROOT}/src/deckpoint/annotations/AnnotationList.cpp
)

target_include_directories(NotesExportTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(NotesExportTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(NotesExportTest)
