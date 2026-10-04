add_executable(KOReaderXPathResolverTest
  KOReaderXPathResolverTest.cpp
  KOReaderXPointerTest.cpp
  ${REPO_ROOT}/lib/KOReaderSync/ChapterXPathResolver.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

target_include_directories(KOReaderXPathResolverTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/KOReaderSync
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/XmlParserUtils
  ${REPO_ROOT}/lib/expat
)

target_compile_definitions(KOReaderXPathResolverTest PRIVATE
  XML_CONTEXT_BYTES=1024
  XML_GE=0
)

# DECKPOINT: extract the bundled test EPUBs so the XPointer round-trip runs over real chapters.
find_program(DECKPOINT_UNZIP unzip)
if(DECKPOINT_UNZIP)
  set(DECKPOINT_EPUB_XHTML_DIR "${CMAKE_CURRENT_BINARY_DIR}/epub_xhtml")
  file(MAKE_DIRECTORY ${DECKPOINT_EPUB_XHTML_DIR})
  file(GLOB DECKPOINT_TEST_EPUBS "${REPO_ROOT}/test/epubs/*.epub")
  foreach(epub ${DECKPOINT_TEST_EPUBS})
    get_filename_component(name ${epub} NAME_WE)
    execute_process(COMMAND ${DECKPOINT_UNZIP} -o -qq ${epub} *.xhtml *.html *.htm -d ${DECKPOINT_EPUB_XHTML_DIR}/${name}
                    RESULT_VARIABLE unzip_result OUTPUT_QUIET ERROR_QUIET)
  endforeach()
  target_compile_definitions(KOReaderXPathResolverTest PRIVATE
    DECKPOINT_TEST_EPUB_XHTML_DIR="${DECKPOINT_EPUB_XHTML_DIR}")
endif()

target_link_libraries(KOReaderXPathResolverTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(KOReaderXPathResolverTest)
