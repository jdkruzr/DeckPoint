# DECKPOINT: AnnotationSync-compatible merge (round 2b) and the local flags sidecar.
add_executable(AnnotationMergeTest
  AnnotationMergeTest.cpp
  ${REPO_ROOT}/src/deckpoint/annotations/Annotation.cpp
  ${REPO_ROOT}/src/deckpoint/annotations/AnnotationExport.cpp
  ${REPO_ROOT}/src/deckpoint/annotations/AnnotationGeometry.cpp
  ${REPO_ROOT}/src/deckpoint/annotations/AnnotationJson.cpp
  ${REPO_ROOT}/src/deckpoint/annotations/AnnotationList.cpp
  ${REPO_ROOT}/src/deckpoint/annotations/AnnotationMerge.cpp
  ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/src/StreamingJsonParser.cpp
)

target_include_directories(AnnotationMergeTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/include
)

target_compile_definitions(AnnotationMergeTest PRIVATE
  DECKPOINT_ANNOTATION_FIXTURES="${REPO_ROOT}/test/annotations/fixtures")

target_link_libraries(AnnotationMergeTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(AnnotationMergeTest)
