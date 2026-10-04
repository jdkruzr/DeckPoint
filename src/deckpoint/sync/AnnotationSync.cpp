#include "AnnotationSync.h"

#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <TrustedTime.h>

#include "AnnotationSyncSettingsActivity.h"  // annotationsync::errorMessage
#include "AnnotationSyncStore.h"
#include "WebDavClient.h"
#include "deckpoint/annotations/AnnotationStore.h"
#include "util/Timezones.h"

using deckpoint::annotations::AnnotationList;
using deckpoint::annotations::AnnotationStore;
using deckpoint::annotations::MergeReport;
using deckpoint::annotations::MergeStatus;

namespace annotationsync {

namespace {

// 500 entries (AnnotationList::MAX_ANNOTATIONS) at ~450 B each, with room.
constexpr size_t MAX_REMOTE_BYTES = 512 * 1024;
// SNTP starts on the Wi-Fi join; give it this long if no Date header landed.
constexpr unsigned long CLOCK_WAIT_MS = 3000;

bool clockCurrent() {
  if (trustedtime::isCurrent()) return true;
  const unsigned long deadline = millis() + CLOCK_WAIT_MS;
  while (!trustedtime::isCurrent() && static_cast<long>(deadline - millis()) > 0) delay(100);
  return trustedtime::isCurrent();
}

void takeDate(const WebDavClient::Result& r) {
  if (r.date[0]) trustedtime::applyHttpDate(r.date);
}

Outcome outcomeForMerge(const MergeStatus status) {
  switch (status) {
    case MergeStatus::Ok:
      return Outcome::Synced;
    case MergeStatus::LocalReadOnly:
    case MergeStatus::SnapshotUnreadable:
      return Outcome::LocalUnreadable;
    case MergeStatus::RemoteUnreadable:
      return Outcome::RemoteUnreadable;
    case MergeStatus::OverBudget:
    case MergeStatus::OutOfMemory:
      return Outcome::NoMemory;
  }
  return Outcome::NoMemory;
}

const char* outcomeName(const Outcome o) {
  switch (o) {
    case Outcome::Synced:
      return "ok";
    case Outcome::NoBook:
      return "no-book";
    case Outcome::ClockNotSet:
      return "clock-not-set";
    case Outcome::ChangedOnServer:
      return "changed-on-server";
    case Outcome::Server:
      return "dav-error";
    case Outcome::LocalUnreadable:
      return "local-unreadable";
    case Outcome::RemoteUnreadable:
      return "remote-unreadable";
    case Outcome::NoMemory:
      return "no-memory";
    case Outcome::SdIo:
      return "sd-io";
  }
  return "?";
}

void removeIfPresent(const std::string& file) {
  if (Storage.exists(file.c_str())) Storage.remove(file.c_str());
}

}  // namespace

bool ready() { return ANNOTATION_SYNC_STORE.isEnabled() && ANNOTATION_SYNC_STORE.isConfigured(); }

BookResult syncBook(const std::string& bookPath) {
  BookResult res;
  const unsigned long t0 = millis();
  auto store = AnnotationStore::open(bookPath);
  if (!store) {
    res.outcome = Outcome::NoBook;
    LOG_INF("ASYNC", "Sync skipped: no annotation store for %s", bookPath.c_str());
    return res;
  }
  const std::string& id = store->documentId();
  if (store->readOnly()) {
    res.outcome = Outcome::LocalUnreadable;
    LOG_INF("ASYNC", "Sync doc=%s result=%s (local file not fully loaded; nothing sent)", id.c_str(),
            outcomeName(res.outcome));
    return res;
  }
  // Heap, not stack: TLS transport + 1 KB upload chunk; freed on return,
  // before the KOSync step needs its own TLS session.
  auto client = makeUniqueNoThrow<WebDavClient>();
  if (!client) {
    LOG_ERR("ASYNC", "OOM: WebDavClient");
    res.outcome = Outcome::NoMemory;
    return res;
  }
  const auto& settings = ANNOTATION_SYNC_STORE;
  client->setCredentials(settings.getUsername(), settings.getPassword());
  const std::string url = settings.fileUrl((id + ".json").c_str());
  const std::string download = AnnotationStore::filePath(id, AnnotationStore::DOWNLOAD_SUFFIX);
  const std::string upload = AnnotationStore::filePath(id, AnnotationStore::UPLOAD_SUFFIX);
  Storage.mkdir(AnnotationStore::DIR);

  PutState put;
  int getStatus = -1;
  int putStatus = -1;
  char getEtag[dav::ETAG_MAX] = {};
  char newEtag[dav::ETAG_MAX] = {};
  unsigned conflicts = 0;
  unsigned deletedRemotely = 0;
  unsigned stamped = 0;
  unsigned seeds = 0;

  const auto makeFolder = [&]() {
    const WebDavClient::Result mk = client->mkcol(settings.folderUrl());
    takeDate(mk);
    LOG_INF("ASYNC", "Created folder %s: status %d", settings.folderUrl().c_str(), mk.status);
    if (!mk.ok()) {
      res.outcome = Outcome::Server;
      res.davError = mk.error;
    }
    return mk.ok();
  };

  for (;;) {  // one GET-merge-PUT round per pass (a 412 reruns it once)
    const WebDavClient::Result get = client->get(url, download.c_str(), MAX_REMOTE_BYTES);
    takeDate(get);
    getStatus = get.status;
    memcpy(getEtag, get.etag, sizeof(getEtag));
    const Remote remote = remoteAfterGet(get.error);
    if (remote == Remote::Failed) {
      res.outcome = Outcome::Server;
      res.davError = get.error;
      break;
    }
    if (remote == Remote::FolderMissing && !put.folderCreated) {
      put.folderCreated = true;
      if (!makeFolder()) break;
    }
    if (!clockCurrent()) {
      // Merging now would stamp and compare against a stale clock.
      res.outcome = Outcome::ClockNotSet;
      break;
    }

    // At most one backup per round: the remote version this round may replace.
    bool backedUp = false;
    AnnotationList remoteList;
    if (remote == Remote::Present) {
      if (!AnnotationStore::readMapFile(download, remoteList)) {
        res.outcome = Outcome::SdIo;
        break;
      }
      if (remoteList.readOnly()) {
        res.outcome = Outcome::RemoteUnreadable;
        break;
      }
      if (!store->hasSyncSnapshot()) {
        // Rule 7: before our first upload for this book.
        backedUp = AnnotationStore::backupRemoteFile(id, download);
        if (!backedUp) {
          res.outcome = Outcome::SdIo;
          break;
        }
      }
    }

    const MergeReport report = store->mergeRemote(remoteList);
    if (report.status != MergeStatus::Ok) {
      res.outcome = outcomeForMerge(report.status);
      break;
    }
    res.summary.pulled += report.added + report.updated;
    res.summary.removed += report.deletedLocally;
    conflicts += report.conflicts;
    deletedRemotely += report.deletedRemotely;
    stamped += report.stamped;
    seeds = report.localOnly;
    if (report.removesOrBlanks && remote == Remote::Present && !backedUp) {
      backedUp = AnnotationStore::backupRemoteFile(id, download);
      if (!backedUp) {
        res.outcome = Outcome::SdIo;
        break;
      }
    }
    if (!report.remoteChanged) {
      // Nothing to send. With no remote file there is nothing to snapshot
      // either (an empty snapshot would skip the first-upload backup).
      if (remote == Remote::Present && !store->saveSyncSnapshot()) {
        LOG_ERR("ASYNC", "Snapshot not saved for %s", id.c_str());
      }
      break;
    }

    if (!store->writeUploadFile(upload)) {
      res.outcome = Outcome::SdIo;
      break;
    }
    bool restart = false;
    for (;;) {
      const bool create = remote != Remote::Present;
      const WebDavClient::Result r = client->put(url, upload.c_str(), create ? nullptr : getEtag, create);
      takeDate(r);
      putStatus = r.status;
      const PutNext next = afterPut(r.error, put);
      if (next == PutNext::Done) {
        res.summary.uploaded = true;
        memcpy(newEtag, r.etag, sizeof(newEtag));
        if (!store->saveSyncSnapshot()) LOG_ERR("ASYNC", "Snapshot not saved for %s", id.c_str());
        break;
      }
      if (next == PutNext::CreateFolderRetry) {
        if (makeFolder()) continue;
        break;
      }
      if (next == PutNext::Restart) {
        LOG_INF("ASYNC", "Remote changed since GET (412); syncing again");
        restart = true;
        break;
      }
      if (next == PutNext::GiveUpChanged) {
        res.outcome = Outcome::ChangedOnServer;
      } else {
        res.outcome = Outcome::Server;
        res.davError = r.error;
      }
      break;
    }
    if (!restart) break;
  }

  removeIfPresent(download);
  removeIfPresent(upload);
  LOG_INF("ASYNC",
          "Sync doc=%s get=%d etag=%s +%u -%u local, -%u remote, conflicts=%u stamped=%u seeds=%u put=%d uploaded=%s "
          "new_etag=%s attempts=%u result=%s%s%s %lums",
          id.c_str(), getStatus, getEtag[0] ? getEtag : "-", res.summary.pulled, res.summary.removed, deletedRemotely,
          conflicts, stamped, seeds, putStatus, res.summary.uploaded ? "y" : "n", newEtag[0] ? newEtag : "-",
          static_cast<unsigned>(put.attempt), outcomeName(res.outcome), res.outcome == Outcome::Server ? " dav=" : "",
          res.outcome == Outcome::Server ? errorMessage(res.davError) : "", millis() - t0);
  return res;
}

void describe(const BookResult& result, char* line1, const size_t line1Size, char* line2, const size_t line2Size) {
  if (line2Size > 0) line2[0] = '\0';
  if (result.ok()) {
    const SummaryText text{tr(STR_HL_UP_TO_DATE), tr(STR_HL_UPLOADED), tr(STR_HL_CHANGES), tr(STR_HL_CHANGES_UPLOADED)};
    formatSummary(result.summary, text, line1, line1Size);
    // Stamps are zone-less local time; a UTC device skews newer-wins merges.
    if (!timezones::isChosen()) snprintf(line2, line2Size, "%s", tr(STR_HL_SET_TIMEZONE));
    return;
  }
  snprintf(line1, line1Size, "%s", tr(STR_HL_SYNC_FAILED));
  const char* reason = "";
  switch (result.outcome) {
    case Outcome::NoBook:
      reason = tr(STR_SEL_UNAVAILABLE);
      break;
    case Outcome::ClockNotSet:
      reason = tr(STR_HL_CLOCK_NOT_SET);
      break;
    case Outcome::ChangedOnServer:
      reason = tr(STR_HL_CHANGED_ON_SERVER);
      break;
    case Outcome::Server:
      reason = errorMessage(result.davError);
      break;
    case Outcome::LocalUnreadable:
      reason = tr(STR_HL_LOCAL_UNREADABLE);
      break;
    case Outcome::RemoteUnreadable:
      reason = tr(STR_HL_REMOTE_UNREADABLE);
      break;
    case Outcome::NoMemory:
      reason = tr(STR_DAV_ERR_NO_MEMORY);
      break;
    case Outcome::SdIo:
      reason = tr(STR_DAV_ERR_SD);
      break;
    case Outcome::Synced:
      break;
  }
  snprintf(line2, line2Size, "%s", reason);
}

}  // namespace annotationsync
