#include "WebDavProtocol.h"

#include <Memory.h>
#include <XmlListParser.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace dav {

Error errorForStatus(const int status) {
  if (status < 0) return Error::Network;
  if (status >= 200 && status < 300) return Error::None;
  switch (status) {
    case 401:
    case 403:
      return Error::Auth;
    case 404:
    case 410:
      return Error::NotFound;
    case 405:
    case 409:
      return Error::Conflict;
    case 412:
      return Error::PreconditionFailed;
    case 413:
      return Error::TooLarge;
    default:
      break;
  }
  return status >= 500 ? Error::Server : Error::Unexpected;
}

std::string baseUrl(const std::string& serverUrl) {
  size_t b = 0;
  size_t e = serverUrl.size();
  while (b < e && isspace(static_cast<unsigned char>(serverUrl[b]))) b++;
  while (e > b && (isspace(static_cast<unsigned char>(serverUrl[e - 1])) || serverUrl[e - 1] == '/')) e--;
  if (b == e) return {};
  std::string url = serverUrl.substr(b, e - b);
  // Nextcloud is reached over TLS in practice; plain http must be explicit.
  if (url.find("://") == std::string::npos) url.insert(0, "https://");
  return url;
}

std::string encodePath(const std::string& path) {
  std::string out;
  out.reserve(path.size() + path.size() / 2);
  for (const unsigned char c : path) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
      out += static_cast<char>(c);
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", c);
      out += buf;
    }
  }
  return out;
}

std::string folderUrl(const std::string& serverUrl, const std::string& folder) {
  std::string url = baseUrl(serverUrl);
  if (url.empty()) return url;
  size_t b = 0;
  size_t e = folder.size();
  while (b < e && (folder[b] == '/' || isspace(static_cast<unsigned char>(folder[b])))) b++;
  while (e > b && (folder[e - 1] == '/' || isspace(static_cast<unsigned char>(folder[e - 1])))) e--;
  url += '/';
  if (b < e) {
    url += encodePath(folder.substr(b, e - b));
    url += '/';
  }
  return url;
}

std::string fileUrl(const std::string& serverUrl, const std::string& folder, const std::string& fileName) {
  std::string url = folderUrl(serverUrl, folder);
  if (url.empty()) return url;
  size_t b = 0;
  while (b < fileName.size() && fileName[b] == '/') b++;
  url += encodePath(fileName.substr(b));
  return url;
}

void ifMatchValue(const char* etag, char* out, const size_t outSize) {
  if (outSize == 0) return;
  out[0] = '\0';
  if (!etag) return;
  while (*etag == ' ' || *etag == '\t') etag++;
  if ((etag[0] == 'W' || etag[0] == 'w') && etag[1] == '/') etag += 2;
  snprintf(out, outSize, "%s", etag);
}

bool copyHeader(const std::string& src, char* out, const size_t outSize) {
  if (outSize == 0) return false;
  if (src.size() >= outSize) {
    out[0] = '\0';
    return false;
  }
  memcpy(out, src.data(), src.size());
  out[src.size()] = '\0';
  return true;
}

namespace {
struct BufferReader {
  const char* data;
  size_t len;
  size_t pos;
};

struct PropsSink {
  Props* props;
  bool seen;
};
}  // namespace

bool parsePropfind(const char* xml, const size_t len, Props& out) {
  out = Props{};
  // XmlListParser matches by local name, so "d:getetag" and "D:getetag" both
  // hit. The id slot carries the ETag, the author slot the modification date
  // (neither has a fallback, unlike the title slot).
  static const std::string kHref = "href";
  static const std::string kNone;
  static const std::string kLastModified = "getlastmodified";
  static const std::string kEtag = "getetag";
  const std::string* const selectors[XmlListParser::F_COUNT] = {&kHref, &kNone, &kLastModified, &kEtag};
  PropsSink sink{&out, false};
  // Heap: the parser carries a 2 KB read buffer, too large for the stack.
  auto parser = makeUniqueNoThrow<XmlListParser>(
      "response", "collection", selectors,
      [](void* ctx, XmlListParser::RawItem& row) {
        auto* s = static_cast<PropsSink*>(ctx);
        if (s->seen) return;  // Depth: 0 describes one resource; ignore extras
        s->seen = true;
        s->props->exists = true;
        s->props->isCollection = row.isDir;
        copyHeader(row.field[XmlListParser::F_ID], s->props->etag, sizeof(s->props->etag));
        copyHeader(row.field[XmlListParser::F_AUTHOR], s->props->lastModified, sizeof(s->props->lastModified));
      },
      &sink);
  if (!parser) return false;
  BufferReader reader{xml, len, 0};
  return parser->parse(
      [](void* ctx, char* buf, const size_t want) -> int {
        auto* r = static_cast<BufferReader*>(ctx);
        const size_t n = std::min(want, r->len - r->pos);
        memcpy(buf, r->data + r->pos, n);
        r->pos += n;
        return static_cast<int>(n);
      },
      &reader);
}

}  // namespace dav
