#include "WebDavClient.h"

#include <HalMemory.h>
#include <HalStorage.h>
#include <Logging.h>

#include "network/WifiPowerSaveGuard.h"

namespace {
// Same preflight margins as the KOSync client (KOReaderSyncClient.cpp): free
// heap floor plus one contiguous block for a full TLS record.
constexpr uint32_t MIN_FREE_FOR_TLS = 35000;
constexpr uint32_t MIN_BLOCK_FOR_TLS = 20000;
constexpr uint32_t TIMEOUT_MS = 20000;
constexpr int MAX_REDIRECTS = 2;

bool isHttps(const std::string& url) { return url.compare(0, 8, "https://") == 0; }
}  // namespace

bool WebDavClient::FileSource::rewind() { return file.seekSet(0); }

int WebDavClient::FileSource::next(const uint8_t** data) {
  const int n = file.read(buf, bufSize);
  *data = buf;
  return n;
}

void WebDavClient::setCredentials(const std::string& u, const std::string& p) {
  user = u;
  pass = p;
}

void WebDavClient::end() {
  http.end();
  sessionLive = false;
}

bool WebDavClient::prepare(const std::string& url, Result& r) {
  if (url.empty() || (!isHttps(url) && url.compare(0, 7, "http://") != 0)) {
    r.error = dav::Error::BadUrl;
    return false;
  }
  if (isHttps(url)) {
    if (!freeink::SecureHttpClient::tls13Available()) {
      r.error = dav::Error::Tls;
      return false;
    }
    if (!sessionLive) {
      const auto heap = HalMemory::getDefaultHeap();
      if (heap.freeBytes < MIN_FREE_FOR_TLS || heap.largestBlockBytes < MIN_BLOCK_FOR_TLS) {
        LOG_ERR("DAV", "Heap too low for TLS: %zu free (need %u), %zu block (need %u)", heap.freeBytes,
                MIN_FREE_FOR_TLS, heap.largestBlockBytes, MIN_BLOCK_FOR_TLS);
        r.error = dav::Error::NoMemory;
        return false;
      }
    }
  }
  http.setUserAgent("DeckPoint");
  http.setInsecure();  // no CA bundle in the transport, as for every SecureNet consumer
  http.setTimeout(TIMEOUT_MS);
  http.setFollowRedirects(MAX_REDIRECTS);
  http.setBasicAuth(user, pass);
  if (!http.begin(url)) {
    r.error = dav::Error::BadUrl;
    return false;
  }
  return true;
}

void WebDavClient::finish(const int status, Result& r) {
  r.status = status;
  r.error = dav::errorForStatus(status);
  if (status >= 0 && !http.responseComplete() && r.error == dav::Error::None) r.error = dav::Error::Network;
  sessionLive = status >= 0;
  if (status < 0) return;
  if (!dav::copyHeader(http.getHeader("etag"), r.etag, sizeof(r.etag))) LOG_ERR("DAV", "ETag too long, dropped");
  dav::copyHeader(http.getHeader("date"), r.date, sizeof(r.date));
}

WebDavClient::Result WebDavClient::get(const std::string& url, const char* destPath, const size_t maxBytes) {
  Result r;
  if (!prepare(url, r)) return r;
  WifiPowerSaveGuard psGuard;

  bool opened = false;
  bool writeFailed = false;
  bool tooLarge = false;
  size_t written = 0;
  size_t drained = 0;
  int status;
  {
    HalFile file;
    status = http.sendRequest("GET", nullptr, 0, [&](const uint8_t* data, const size_t len) {
      if (http.getStatus() != 200) {
        drained += len;
        return drained <= MAX_DRAIN;
      }
      if (!opened) {
        if (!Storage.openFileForWrite("DAV", destPath, file)) {
          writeFailed = true;
          return false;
        }
        opened = true;
      }
      if (written + len > maxBytes) {
        tooLarge = true;
        return false;
      }
      if (file.write(data, len) != len) {
        writeFailed = true;
        return false;
      }
      written += len;
      return true;
    });
    if (opened) file.flush();
    // file closes here, before any remove of destPath
  }
  finish(status, r);
  if (status == 200) {
    // Our own abort leaves the body incomplete, which finish() calls Network.
    if (tooLarge) r.error = dav::Error::TooLarge;
    if (writeFailed) r.error = dav::Error::SdIo;
  } else if (r.ok()) {
    r.error = dav::Error::Unexpected;  // 204 / 206: nothing usable
  }

  if (r.ok() && !opened) {
    // Empty 200 body: still leave an (empty) file behind.
    HalFile empty;
    if (!Storage.openFileForWrite("DAV", destPath, empty)) r.error = dav::Error::SdIo;
  }
  if (!r.ok() && opened) Storage.remove(destPath);
  LOG_DBG("DAV", "GET %s -> %d (%zu bytes)", url.c_str(), status, written);
  return r;
}

WebDavClient::Result WebDavClient::put(const std::string& url, const char* srcPath, const char* ifMatchEtag,
                                       const bool ifNoneMatchAny) {
  Result r;
  HalFile file;
  if (!Storage.openFileForRead("DAV", srcPath, file)) {
    r.error = dav::Error::SdIo;
    return r;
  }
  FileSource source(file, chunk, sizeof(chunk));
  source.total = file.fileSize();
  if (!prepare(url, r)) return r;
  WifiPowerSaveGuard psGuard;

  http.addHeader("Content-Type", "application/json");
  char ifMatch[dav::ETAG_MAX];
  dav::ifMatchValue(ifMatchEtag, ifMatch, sizeof(ifMatch));
  if (ifMatch[0]) http.addHeader("If-Match", ifMatch);
  if (ifNoneMatchAny) http.addHeader("If-None-Match", "*");
  http.setBodySource(&source);

  size_t drained = 0;
  const int status = http.sendRequest("PUT", nullptr, 0, [&](const uint8_t*, const size_t len) {
    drained += len;
    return drained <= MAX_DRAIN;
  });
  finish(status, r);
  LOG_DBG("DAV", "PUT %s (%zu bytes) -> %d", url.c_str(), source.total, status);

  if (r.ok() && !r.etag[0]) {
    // Some servers answer a PUT without an ETag; read it back so the next
    // conditional upload has one. Keep the PUT's own status and Date.
    dav::Props props;
    const Result probe = propfind(url, props);
    if (probe.ok()) memcpy(r.etag, props.etag, sizeof(r.etag));
  }
  return r;
}

WebDavClient::Result WebDavClient::propfind(const std::string& url, dav::Props& props) {
  props = dav::Props{};
  Result r;
  if (!prepare(url, r)) return r;
  WifiPowerSaveGuard psGuard;

  http.addHeader("Depth", "0");
  http.addHeader("Content-Type", "application/xml; charset=utf-8");
  // A Depth: 0 multistatus for one resource is well under 1 KB; reserve that
  // once instead of growing per chunk.
  std::string body;
  body.reserve(1024);
  bool overflow = false;
  size_t drained = 0;
  const int status = http.sendRequest("PROPFIND", reinterpret_cast<const uint8_t*>(dav::PROPFIND_BODY),
                                      sizeof(dav::PROPFIND_BODY) - 1, [&](const uint8_t* data, const size_t len) {
                                        if (http.getStatus() != 207) {
                                          drained += len;
                                          return drained <= MAX_DRAIN;
                                        }
                                        if (body.size() + len > MAX_PROPFIND_RESPONSE) {
                                          overflow = true;
                                          return false;
                                        }
                                        body.append(reinterpret_cast<const char*>(data), len);
                                        return true;
                                      });
  finish(status, r);
  LOG_DBG("DAV", "PROPFIND %s -> %d", url.c_str(), status);
  if (!r.ok()) return r;
  if (overflow) {
    r.error = dav::Error::TooLarge;
    return r;
  }
  if (status != 207) {
    r.error = dav::Error::Unexpected;
    return r;
  }
  if (!dav::parsePropfind(body.data(), body.size(), props)) {
    LOG_ERR("DAV", "PROPFIND response did not parse");
    r.error = dav::Error::Unexpected;
    return r;
  }
  if (!props.exists) r.error = dav::Error::NotFound;
  return r;
}

WebDavClient::Result WebDavClient::mkcol(const std::string& url) {
  Result r;
  if (!prepare(url, r)) return r;
  WifiPowerSaveGuard psGuard;
  size_t drained = 0;
  const int status = http.sendRequest("MKCOL", nullptr, 0, [&](const uint8_t*, const size_t len) {
    drained += len;
    return drained <= MAX_DRAIN;
  });
  finish(status, r);
  if (status == 405) r.error = dav::Error::None;  // the collection already exists
  LOG_DBG("DAV", "MKCOL %s -> %d", url.c_str(), status);
  return r;
}
