# DECKPOINT: per-word visible offsets (TextBlock) and their XPointer round trip.
add_executable(WordOffsetsTest
  WordOffsetsTest.cpp
  WordOffsetsLinkStubs.cpp
  ${REPO_ROOT}/lib/Epub/Epub/Page.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp
  ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp
  ${REPO_ROOT}/lib/MiniBidi/minibidi.c
  ${REPO_ROOT}/lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/css/CssParser.cpp
  ${REPO_ROOT}/lib/Epub/Epub/ParsedText.cpp
  ${REPO_ROOT}/lib/Epub/Epub/WordStore.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageDimsProbe.cpp
  ${REPO_ROOT}/lib/KOReaderSync/ChapterXPathResolver.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
)

target_include_directories(WordOffsetsTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/test/chapter_html_slim_parser/stubs
  ${REPO_ROOT}/test/koreader_xpath_resolver
  ${REPO_ROOT}/lib
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/Epub/Epub/parsers
  ${REPO_ROOT}/lib/Epub/Epub/css
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/KOReaderSync
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/lib/XmlParserUtils
  ${REPO_ROOT}/test/fs_helpers/stubs
)

# Extracted by test/koreader_xpath_resolver at configure time (when unzip exists).
target_compile_definitions(WordOffsetsTest PRIVATE
  DECKPOINT_TEST_EPUB_XHTML_DIR="${CMAKE_BINARY_DIR}/koreader_xpath_resolver/epub_xhtml")

target_link_libraries(WordOffsetsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
  expat
)

gtest_discover_tests(WordOffsetsTest)
