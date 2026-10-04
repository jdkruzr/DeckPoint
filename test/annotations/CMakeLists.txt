# DECKPOINT: annotation model, AnnotationSync JSON, caps and underline plan.
add_executable(AnnotationsTest
  AnnotationsTest.cpp
  ${REPO_ROOT}/src/deckpoint/annotations/Annotation.cpp
  ${REPO_ROOT}/src/deckpoint/annotations/AnnotationGeometry.cpp
  ${REPO_ROOT}/src/deckpoint/annotations/AnnotationJson.cpp
  ${REPO_ROOT}/src/deckpoint/annotations/AnnotationList.cpp
  ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/src/StreamingJsonParser.cpp
)

target_include_directories(AnnotationsTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/include
)

target_compile_definitions(AnnotationsTest PRIVATE
  DECKPOINT_ANNOTATION_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

target_link_libraries(AnnotationsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(AnnotationsTest)
