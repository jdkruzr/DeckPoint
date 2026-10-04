#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// DECKPOINT: transport-free WebDAV pieces (URL building, status mapping,
// ETag handling, PROPFIND parsing) shared by WebDavClient and host tests.
namespace dav {

enum class Error : uint8_t {
  None,
  Auth,                // 401 / 403
  NotFound,            // 404 / 410, or a PROPFIND that listed nothing
  Conflict,            // 409 (missing parent collection), 405 (MKCOL on an existing one)
  PreconditionFailed,  // 412: If-Match / If-None-Match did not hold
  Network,             // no HTTP status: DNS, TCP, TLS handshake, timeout, truncated body
  Tls,                 // https requested on a build without TLS support
  Server,              // 5xx
  NoMemory,            // heap too low for a TLS handshake, or an allocation failed
  BadUrl,              // URL not set or not http(s)
  SdIo,                // SD read/write failed (named so: `Storage` is the HalStorage macro)
  TooLarge,            // body over the caller's cap, or 413
  Unexpected,          // any other status (unfollowed 3xx, other 4xx)
};

// Header values: an IMF-fixdate is 29 chars; ETags from Nextcloud/Apache/
// nginx are well under 64.
constexpr size_t ETAG_MAX = 96;
constexpr size_t DATE_MAX = 40;

struct Props {
  bool exists = false;
  bool isCollection = false;
  char etag[ETAG_MAX] = {};          // as served, quotes kept ("" when absent)
  char lastModified[DATE_MAX] = {};  // getlastmodified, RFC 1123
};

// HTTP status -> error. Any 2xx (incl. 207 Multi-Status) is None; < 0 is Network.
Error errorForStatus(int status);

// "https://host/remote.php/dav/files/u/" -> "https://host/remote.php/dav/files/u".
// Trims whitespace and trailing slashes; a missing scheme becomes https://.
// Empty input stays empty.
std::string baseUrl(const std::string& serverUrl);

// Percent-encodes everything but RFC 3986 unreserved characters and '/'
// (KOReader's util.urlEncode(path, "/")).
std::string encodePath(const std::string& path);

// base + "/" + encoded folder (leading/trailing slashes trimmed) + "/".
// Collections take the trailing slash; an empty folder is the base itself.
std::string folderUrl(const std::string& serverUrl, const std::string& folder);

// folderUrl + encoded file name: AnnotationSync's "<folder>/<partialMD5>.json".
std::string fileUrl(const std::string& serverUrl, const std::string& folder, const std::string& fileName);

// Value for If-Match: strips a weak "W/" prefix (If-Match compares strongly,
// so a weak validator would 412 forever; KOReader does the same). Writes ""
// when etag is null or empty.
void ifMatchValue(const char* etag, char* out, size_t outSize);

// Copies src into a fixed buffer. Returns false (and leaves out empty) when
// src does not fit: a truncated ETag is worse than none.
bool copyHeader(const std::string& src, char* out, size_t outSize);

// PROPFIND request body asking for resourcetype, getetag and getlastmodified.
inline constexpr char PROPFIND_BODY[] =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<d:propfind xmlns:d=\"DAV:\"><d:prop><d:resourcetype/><d:getetag/><d:getlastmodified/></d:prop></d:propfind>";

// Parses a Depth: 0 multistatus (first <response> only; prefixes such as d:
// or D: ignored). Returns false on malformed XML or parser OOM.
bool parsePropfind(const char* xml, size_t len, Props& out);

}  // namespace dav
