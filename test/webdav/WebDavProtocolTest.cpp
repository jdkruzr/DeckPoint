#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "deckpoint/sync/WebDavProtocol.h"

namespace {

bool parse(const std::string& xml, dav::Props& p) { return dav::parsePropfind(xml.data(), xml.size(), p); }

// Shape of a real Nextcloud 28 answer to a Depth: 0 PROPFIND on a folder.
const char NEXTCLOUD_FOLDER[] = R"(<?xml version="1.0"?>
<d:multistatus xmlns:d="DAV:" xmlns:s="http://sabredav.org/ns" xmlns:oc="http://owncloud.org/ns" xmlns:nc="http://nextcloud.org/ns">
  <d:response>
    <d:href>/remote.php/dav/files/jtd/eBooks/</d:href>
    <d:propstat>
      <d:prop>
        <d:resourcetype><d:collection/></d:resourcetype>
        <d:getetag>&quot;66f8a3c1d2e4b&quot;</d:getetag>
        <d:getlastmodified>Sat, 28 Sep 2026 18:02:09 GMT</d:getlastmodified>
      </d:prop>
      <d:status>HTTP/1.1 200 OK</d:status>
    </d:propstat>
  </d:response>
</d:multistatus>)";

const char NEXTCLOUD_FILE[] = R"(<?xml version="1.0"?>
<d:multistatus xmlns:d="DAV:" xmlns:oc="http://owncloud.org/ns">
 <d:response>
  <d:href>/remote.php/dav/files/jtd/eBooks/5d09b7f28da63ec6d4a621fae1158d4c.json</d:href>
  <d:propstat>
   <d:prop>
    <d:resourcetype/>
    <d:getetag>&quot;8c1f0e2a7b3d4f5e6a7b8c9d0e1f2a3b&quot;</d:getetag>
    <d:getlastmodified>Sun, 04 Oct 2026 10:11:12 GMT</d:getlastmodified>
   </d:prop>
   <d:status>HTTP/1.1 200 OK</d:status>
  </d:propstat>
 </d:response>
</d:multistatus>)";

}  // namespace

TEST(WebDavUrl, BaseUrlTrimsAndAddsScheme) {
  EXPECT_EQ(dav::baseUrl("https://cloud.example/remote.php/dav/files/u/"),
            "https://cloud.example/remote.php/dav/files/u");
  EXPECT_EQ(dav::baseUrl("  cloud.example/dav//  "), "https://cloud.example/dav");
  EXPECT_EQ(dav::baseUrl("http://10.0.0.2:8080"), "http://10.0.0.2:8080");
  EXPECT_EQ(dav::baseUrl(""), "");
  EXPECT_EQ(dav::baseUrl(" / "), "");
}

TEST(WebDavUrl, FolderUrlJoinsWithOneSlash) {
  const std::string want = "https://h/remote.php/dav/files/u/eBooks/";
  EXPECT_EQ(dav::folderUrl("https://h/remote.php/dav/files/u/", "/eBooks"), want);
  EXPECT_EQ(dav::folderUrl("https://h/remote.php/dav/files/u", "eBooks/"), want);
  EXPECT_EQ(dav::folderUrl("https://h/remote.php/dav/files/u//", "//eBooks//"), want);
  EXPECT_EQ(dav::folderUrl("https://h/dav", ""), "https://h/dav/");
  EXPECT_EQ(dav::folderUrl("https://h/dav", "/"), "https://h/dav/");
  EXPECT_EQ(dav::folderUrl("https://h/dav", "Books/Sync notes"), "https://h/dav/Books/Sync%20notes/");
  EXPECT_EQ(dav::folderUrl("", "/eBooks"), "");
}

TEST(WebDavUrl, FileUrlMatchesAnnotationSyncLayout) {
  EXPECT_EQ(dav::fileUrl("https://h/remote.php/dav/files/u/", "/eBooks", "5d09b7f28da63ec6d4a621fae1158d4c.json"),
            "https://h/remote.php/dav/files/u/eBooks/5d09b7f28da63ec6d4a621fae1158d4c.json");
  EXPECT_EQ(dav::fileUrl("https://h/dav", "", "/a.json"), "https://h/dav/a.json");
}

TEST(WebDavUrl, EncodePathKeepsUnreservedAndSlash) {
  EXPECT_EQ(dav::encodePath("a-b_c.d~e/f"), "a-b_c.d~e/f");
  EXPECT_EQ(dav::encodePath("Sci Fi/Ünïcode?#%&+"), "Sci%20Fi/%C3%9Cn%C3%AFcode%3F%23%25%26%2B");
}

TEST(WebDavStatus, MapsToErrors) {
  EXPECT_EQ(dav::errorForStatus(200), dav::Error::None);
  EXPECT_EQ(dav::errorForStatus(201), dav::Error::None);
  EXPECT_EQ(dav::errorForStatus(204), dav::Error::None);
  EXPECT_EQ(dav::errorForStatus(207), dav::Error::None);
  EXPECT_EQ(dav::errorForStatus(401), dav::Error::Auth);
  EXPECT_EQ(dav::errorForStatus(403), dav::Error::Auth);
  EXPECT_EQ(dav::errorForStatus(404), dav::Error::NotFound);
  EXPECT_EQ(dav::errorForStatus(410), dav::Error::NotFound);
  EXPECT_EQ(dav::errorForStatus(405), dav::Error::Conflict);
  EXPECT_EQ(dav::errorForStatus(409), dav::Error::Conflict);
  EXPECT_EQ(dav::errorForStatus(412), dav::Error::PreconditionFailed);
  EXPECT_EQ(dav::errorForStatus(413), dav::Error::TooLarge);
  EXPECT_EQ(dav::errorForStatus(500), dav::Error::Server);
  EXPECT_EQ(dav::errorForStatus(503), dav::Error::Server);
  EXPECT_EQ(dav::errorForStatus(507), dav::Error::Server);
  EXPECT_EQ(dav::errorForStatus(302), dav::Error::Unexpected);
  EXPECT_EQ(dav::errorForStatus(400), dav::Error::Unexpected);
  EXPECT_EQ(dav::errorForStatus(-1), dav::Error::Network);
}

TEST(WebDavEtag, IfMatchStripsWeakPrefixOnly) {
  char out[dav::ETAG_MAX];
  dav::ifMatchValue("\"abc\"", out, sizeof(out));
  EXPECT_STREQ(out, "\"abc\"");
  dav::ifMatchValue("W/\"abc\"", out, sizeof(out));
  EXPECT_STREQ(out, "\"abc\"");
  dav::ifMatchValue(" w/\"abc\"", out, sizeof(out));
  EXPECT_STREQ(out, "\"abc\"");
  dav::ifMatchValue(nullptr, out, sizeof(out));
  EXPECT_STREQ(out, "");
  dav::ifMatchValue("", out, sizeof(out));
  EXPECT_STREQ(out, "");
}

TEST(WebDavEtag, CopyHeaderRefusesTruncation) {
  char out[8];
  EXPECT_TRUE(dav::copyHeader("\"1234\"", out, sizeof(out)));
  EXPECT_STREQ(out, "\"1234\"");
  EXPECT_FALSE(dav::copyHeader("\"123456789\"", out, sizeof(out)));
  EXPECT_STREQ(out, "");
}

TEST(WebDavPropfind, NextcloudFolder) {
  dav::Props p;
  ASSERT_TRUE(parse(NEXTCLOUD_FOLDER, p));
  EXPECT_TRUE(p.exists);
  EXPECT_TRUE(p.isCollection);
  EXPECT_STREQ(p.etag, "\"66f8a3c1d2e4b\"");
  EXPECT_STREQ(p.lastModified, "Sat, 28 Sep 2026 18:02:09 GMT");
}

TEST(WebDavPropfind, NextcloudFile) {
  dav::Props p;
  ASSERT_TRUE(parse(NEXTCLOUD_FILE, p));
  EXPECT_TRUE(p.exists);
  EXPECT_FALSE(p.isCollection);
  EXPECT_STREQ(p.etag, "\"8c1f0e2a7b3d4f5e6a7b8c9d0e1f2a3b\"");
  EXPECT_STREQ(p.lastModified, "Sun, 04 Oct 2026 10:11:12 GMT");
}

TEST(WebDavPropfind, ApacheUppercasePrefixAndWeakEtag) {
  // mod_dav / lighttpd use "D:" and may send weak validators.
  const std::string xml = R"(<?xml version="1.0" encoding="utf-8"?>
<D:multistatus xmlns:D="DAV:" xmlns:ns0="DAV:">
<D:response xmlns:lp1="DAV:" xmlns:lp2="http://apache.org/dav/props/">
<D:href>/dav/eBooks/a.json</D:href>
<D:propstat><D:prop>
<lp1:resourcetype/>
<lp1:getlastmodified>Mon, 05 Oct 2026 08:00:00 GMT</lp1:getlastmodified>
<lp1:getetag>W/"2a-5f1"</lp1:getetag>
</D:prop><D:status>HTTP/1.1 200 OK</D:status></D:propstat>
</D:response></D:multistatus>)";
  dav::Props p;
  ASSERT_TRUE(parse(xml, p));
  EXPECT_TRUE(p.exists);
  EXPECT_FALSE(p.isCollection);
  EXPECT_STREQ(p.etag, "W/\"2a-5f1\"");
  EXPECT_STREQ(p.lastModified, "Mon, 05 Oct 2026 08:00:00 GMT");
}

TEST(WebDavPropfind, MissingPropsLandInEmptyFields) {
  // Unknown properties come back in a 404 propstat as empty elements.
  const std::string xml = R"(<d:multistatus xmlns:d="DAV:"><d:response>
<d:href>/dav/eBooks/</d:href>
<d:propstat><d:prop><d:resourcetype><d:collection/></d:resourcetype></d:prop>
<d:status>HTTP/1.1 200 OK</d:status></d:propstat>
<d:propstat><d:prop><d:getetag/><d:getlastmodified/></d:prop>
<d:status>HTTP/1.1 404 Not Found</d:status></d:propstat>
</d:response></d:multistatus>)";
  dav::Props p;
  ASSERT_TRUE(parse(xml, p));
  EXPECT_TRUE(p.exists);
  EXPECT_TRUE(p.isCollection);
  EXPECT_STREQ(p.etag, "");
  EXPECT_STREQ(p.lastModified, "");
}

TEST(WebDavPropfind, FirstResponseOnlyAndEmptyMultistatus) {
  const std::string two = R"(<d:multistatus xmlns:d="DAV:">
<d:response><d:href>/f/</d:href><d:propstat><d:prop><d:resourcetype><d:collection/></d:resourcetype>
<d:getetag>"one"</d:getetag></d:prop></d:propstat></d:response>
<d:response><d:href>/f/x.json</d:href><d:propstat><d:prop><d:resourcetype/>
<d:getetag>"two"</d:getetag></d:prop></d:propstat></d:response>
</d:multistatus>)";
  dav::Props p;
  ASSERT_TRUE(parse(two, p));
  EXPECT_TRUE(p.isCollection);
  EXPECT_STREQ(p.etag, "\"one\"");

  ASSERT_TRUE(parse(R"(<d:multistatus xmlns:d="DAV:"></d:multistatus>)", p));
  EXPECT_FALSE(p.exists);
}

TEST(WebDavPropfind, MalformedXmlFails) {
  dav::Props p;
  EXPECT_FALSE(parse("<d:multistatus xmlns:d=\"DAV:\"><d:response>", p));
  EXPECT_FALSE(parse("<html><body>Login</body>", p));
}

TEST(WebDavPropfind, RequestBodyIsWellFormed) {
  dav::Props p;
  // The request body parses (no <response>, so nothing found).
  EXPECT_TRUE(dav::parsePropfind(dav::PROPFIND_BODY, strlen(dav::PROPFIND_BODY), p));
  EXPECT_NE(strstr(dav::PROPFIND_BODY, "getetag"), nullptr);
}

// Captured 2026-10-04 from `rclone serve webdav` (getetag of a folder in a 404
// propstat, xmlns redeclared on <collection/>) and Apache mod_dav (lp1: prefix).
TEST(WebDavPropfind, RealRcloneResponses) {
  dav::Props p;
  ASSERT_TRUE(parse(
      R"XML(<?xml version="1.0" encoding="UTF-8"?><D:multistatus xmlns:D="DAV:"><D:response><D:href>/eBooks/</D:href><D:propstat><D:prop><D:resourcetype><D:collection xmlns:D="DAV:"/></D:resourcetype><D:getlastmodified>Sun, 04 Oct 2026 19:37:20 GMT</D:getlastmodified></D:prop><D:status>HTTP/1.1 200 OK</D:status></D:propstat><D:propstat><D:prop><D:getetag></D:getetag></D:prop><D:status>HTTP/1.1 404 Not Found</D:status></D:propstat></D:response></D:multistatus>)XML",
      p));
  EXPECT_TRUE(p.exists);
  EXPECT_TRUE(p.isCollection);
  EXPECT_STREQ(p.etag, "");
  EXPECT_STREQ(p.lastModified, "Sun, 04 Oct 2026 19:37:20 GMT");

  ASSERT_TRUE(parse(
      R"XML(<?xml version="1.0" encoding="UTF-8"?><D:multistatus xmlns:D="DAV:"><D:response><D:href>/eBooks/x.json</D:href><D:propstat><D:prop><D:resourcetype></D:resourcetype><D:getetag>"18db6abf9f3356cf8"</D:getetag><D:getlastmodified>Sun, 04 Oct 2026 19:37:28 GMT</D:getlastmodified></D:prop><D:status>HTTP/1.1 200 OK</D:status></D:propstat></D:response></D:multistatus>)XML",
      p));
  EXPECT_FALSE(p.isCollection);
  EXPECT_STREQ(p.etag, "\"18db6abf9f3356cf8\"");
}

TEST(WebDavPropfind, RealApacheFolder) {
  dav::Props p;
  ASSERT_TRUE(parse(R"XML(<?xml version="1.0" encoding="utf-8"?>
<D:multistatus xmlns:D="DAV:" xmlns:ns0="DAV:">
<D:response xmlns:lp1="DAV:" xmlns:lp2="http://apache.org/dav/props/">
<D:href>/eBooks/</D:href>
<D:propstat>
<D:prop>
<lp1:resourcetype><D:collection/></lp1:resourcetype>
<lp1:getetag>"1000-65d08e67b0dc8"</lp1:getetag>
<lp1:getlastmodified>Sun, 04 Oct 2026 19:37:48 GMT</lp1:getlastmodified>
</D:prop>
<D:status>HTTP/1.1 200 OK</D:status>
</D:propstat>
</D:response>
</D:multistatus>)XML",
                    p));
  EXPECT_TRUE(p.isCollection);
  EXPECT_STREQ(p.etag, "\"1000-65d08e67b0dc8\"");
}
