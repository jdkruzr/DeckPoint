# DECKPOINT: WebDAV URL building, status mapping, ETag handling, PROPFIND parsing.
find_package(EXPAT REQUIRED)

add_executable(WebDavProtocolTest
  WebDavProtocolTest.cpp
  ${REPO_ROOT}/src/deckpoint/sync/WebDavProtocol.cpp
  ${REPO_ROOT}/freeink-sdk/libs/network/CatalogList/src/XmlListParser.cpp
)

target_include_directories(WebDavProtocolTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/freeink-sdk/libs/network/CatalogList/include
)

target_link_libraries(WebDavProtocolTest PRIVATE
  crosspoint_test_common
  EXPAT::EXPAT
  GTest::gtest_main
)

gtest_discover_tests(WebDavProtocolTest)
