#pragma once

#include <SecureHttpClient.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "WebDavProtocol.h"

class HalFile;

// DECKPOINT: minimal WebDAV client for annotation sync (GET / PUT / PROPFIND /
// MKCOL) over SecureHttpClient. Bodies stream between the socket and SD; only
// a Depth: 0 PROPFIND answer (capped at 8 KB) is held in RAM. One instance
// keeps one kept-alive (TLS) connection across calls, so a sync of several
// books pays for one handshake. ~1.3 KB plus the TLS client: allocate it with
// makeUniqueNoThrow, not on a task stack.
//
// URLs are absolute and already encoded: build them with dav::folderUrl /
// dav::fileUrl (or AnnotationSyncStore::folderUrl / fileUrl).
class WebDavClient {
 public:
  struct Result {
    dav::Error error = dav::Error::Network;
    int status = -1;                // HTTP status of the final response; < 0 = no response
    char etag[dav::ETAG_MAX] = {};  // ETag response header, verbatim ("" when absent)
    char date[dav::DATE_MAX] = {};  // Date response header (IMF-fixdate), the server's clock
    bool ok() const { return error == dav::Error::None; }
  };

  WebDavClient() = default;
  ~WebDavClient() { end(); }
  WebDavClient(const WebDavClient&) = delete;
  WebDavClient& operator=(const WebDavClient&) = delete;

  // Basic-auth credentials sent with every request.
  void setCredentials(const std::string& user, const std::string& pass);

  // Streams the body of a 200 into destPath (overwritten; removed again on any
  // failure). maxBytes caps the download (TooLarge past it). A 404 is
  // Error::NotFound with destPath untouched.
  Result get(const std::string& url, const char* destPath, size_t maxBytes);

  // Uploads srcPath. ifMatchEtag (weak prefix stripped) makes the write
  // conditional on the remote being unchanged since that ETag; ifNoneMatchAny
  // sends "If-None-Match: *" (create only). Either failing is
  // PreconditionFailed. On success etag holds the new ETag (from the response,
  // or a follow-up PROPFIND when the server sends none).
  Result put(const std::string& url, const char* srcPath, const char* ifMatchEtag, bool ifNoneMatchAny);

  // Depth: 0 PROPFIND. 207 fills props (exists, collection, etag,
  // lastModified); 404 is NotFound with props.exists == false.
  Result propfind(const std::string& url, dav::Props& props);

  // Creates a collection. 405 (already exists) also counts as success.
  Result mkcol(const std::string& url);

  // Drops the kept-alive connection.
  void end();

 private:
  // Streams a file as the request body through a heap-resident chunk buffer.
  class FileSource final : public freeink::SecureHttpClient::BodySource {
   public:
    FileSource(HalFile& file, uint8_t* buf, size_t bufSize) : file(file), buf(buf), bufSize(bufSize) {}
    size_t size() const override { return total; }
    bool rewind() override;
    int next(const uint8_t** data) override;
    size_t total = 0;

   private:
    HalFile& file;
    uint8_t* buf;
    size_t bufSize;
  };

  // Begins a request: URL, auth, timeouts, heap guard. False fills r.error.
  bool prepare(const std::string& url, Result& r);
  // Status, error, ETag and Date from the finished response.
  void finish(int status, Result& r);

  static constexpr size_t UPLOAD_CHUNK = 1024;
  static constexpr size_t MAX_PROPFIND_RESPONSE = 8 * 1024;
  // Error bodies (HTML pages) are drained so the connection stays reusable,
  // but only up to this much.
  static constexpr size_t MAX_DRAIN = 16 * 1024;

  freeink::SecureHttpClient http;
  std::string user;
  std::string pass;
  bool sessionLive = false;  // a TLS session may be open: skip the pre-handshake heap guard
  uint8_t chunk[UPLOAD_CHUNK] = {};
};
