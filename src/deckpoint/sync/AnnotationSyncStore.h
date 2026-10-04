#pragma once

#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>

// DECKPOINT: WebDAV settings for annotation sync (AnnotationSync.koplugin
// layout: one "<folder>/<partialMD5>.json" per book). Stored on SD like
// KOReaderCredentialStore: password XOR-obfuscated with the device key and
// base64-encoded, plus length and CRC-32 so a corrupted value is dropped
// instead of being sent (WifiCredentialStore's integrity check).
class AnnotationSyncStore : public PersistableStore<AnnotationSyncStore> {
  AnnotationSyncStore() = default;
  ~AnnotationSyncStore() = default;
  friend class PersistableStore<AnnotationSyncStore>;

  bool enabled = false;
  std::string serverUrl;  // WebDAV root, e.g. https://host/remote.php/dav/files/<user>/
  std::string username;
  std::string password;  // Nextcloud app password
  std::string folder;    // e.g. /eBooks

 public:
  static constexpr size_t MAX_URL = 256;
  static constexpr size_t MAX_FIELD = 128;

  static const char* getFilePath() { return "/.crosspoint/annotation_sync.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  bool isEnabled() const { return enabled; }
  void setEnabled(bool on) { enabled = on; }
  const std::string& getServerUrl() const { return serverUrl; }
  void setServerUrl(const std::string& url) { serverUrl = url; }
  const std::string& getUsername() const { return username; }
  void setUsername(const std::string& user) { username = user; }
  const std::string& getPassword() const { return password; }
  void setPassword(const std::string& pass) { password = pass; }
  const std::string& getFolder() const { return folder; }
  void setFolder(const std::string& f) { folder = f; }

  // URL, user, password and folder all set.
  bool isConfigured() const;
  // The folder as a collection URL (trailing slash).
  std::string folderUrl() const;
  // "<folder>/<name>", e.g. fileUrl("5d09...4c.json").
  std::string fileUrl(const char* name) const;
};

#define ANNOTATION_SYNC_STORE AnnotationSyncStore::getInstance()
