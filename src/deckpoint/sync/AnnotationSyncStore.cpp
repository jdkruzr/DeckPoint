#include "AnnotationSyncStore.h"

#include <CredentialIntegrity.h>
#include <Logging.h>
#include <ObfuscationUtils.h>

#include "WebDavProtocol.h"

void AnnotationSyncStore::toJson(JsonDocument& doc) const {
  doc["enabled"] = enabled;
  doc["serverUrl"] = serverUrl;
  doc["username"] = username;
  doc["password_obf"] = obfuscation::obfuscateToBase64(password);
  doc["password_len"] = password.size();
  doc["password_crc32"] = credential_integrity::crc32(password);
  doc["folder"] = folder;
}

bool AnnotationSyncStore::fromJson(JsonVariantConst doc) {
  enabled = doc["enabled"] | false;
  serverUrl = doc["serverUrl"] | "";
  username = doc["username"] | "";
  folder = doc["folder"] | "";
  if (serverUrl.size() > MAX_URL) serverUrl.clear();
  if (username.size() > MAX_FIELD) username.clear();
  if (folder.size() > MAX_FIELD) folder.clear();

  bool needsResave = false;
  bool valid = false;
  password = extractPassword(doc, needsResave, MAX_FIELD, valid);
  const JsonVariantConst len = doc["password_len"];
  const JsonVariantConst crc = doc["password_crc32"];
  if (!valid || !len.is<size_t>() || !crc.is<uint32_t>() ||
      !credential_integrity::validate(password, len.as<size_t>(), crc.as<uint32_t>())) {
    // A wrong password would lock the account after a few syncs; drop it and
    // let the user re-enter it.
    if (!password.empty() || !valid) LOG_ERR("ASYNC", "Discarding corrupted WebDAV password");
    password.clear();
    needsResave = true;
  }
  if (needsResave) requestResave();
  return true;
}

bool AnnotationSyncStore::isConfigured() const {
  return !serverUrl.empty() && !username.empty() && !password.empty() && !folder.empty();
}

std::string AnnotationSyncStore::folderUrl() const { return dav::folderUrl(serverUrl, folder); }

std::string AnnotationSyncStore::fileUrl(const char* name) const { return dav::fileUrl(serverUrl, folder, name); }
