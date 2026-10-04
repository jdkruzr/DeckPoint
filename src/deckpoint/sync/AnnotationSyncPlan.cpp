#include "AnnotationSyncPlan.h"

#include <cstdio>

namespace annotationsync {

Remote remoteAfterGet(const dav::Error error) {
  switch (error) {
    case dav::Error::None:
      return Remote::Present;
    case dav::Error::NotFound:
      return Remote::Absent;
    case dav::Error::Conflict:
      return Remote::FolderMissing;
    default:
      return Remote::Failed;
  }
}

PutNext afterPut(const dav::Error error, PutState& state) {
  switch (error) {
    case dav::Error::None:
      return PutNext::Done;
    case dav::Error::PreconditionFailed:
      if (state.attempt >= MAX_ATTEMPTS) return PutNext::GiveUpChanged;
      state.attempt++;
      return PutNext::Restart;
    case dav::Error::Conflict:
    case dav::Error::NotFound:
      if (state.folderCreated) return PutNext::Fail;
      state.folderCreated = true;
      return PutNext::CreateFolderRetry;
    default:
      return PutNext::Fail;
  }
}

void formatSummary(const Summary& summary, const SummaryText& text, char* out, const size_t outSize) {
  if (outSize == 0) return;
  if (summary.pulled == 0 && summary.removed == 0) {
    snprintf(out, outSize, "%s", summary.uploaded ? text.uploadedOnly : text.upToDate);
    return;
  }
  char counts[24];
  if (summary.pulled > 0 && summary.removed > 0) {
    snprintf(counts, sizeof(counts), "+%u -%u", summary.pulled, summary.removed);
  } else if (summary.pulled > 0) {
    snprintf(counts, sizeof(counts), "+%u", summary.pulled);
  } else {
    snprintf(counts, sizeof(counts), "-%u", summary.removed);
  }
  snprintf(out, outSize, summary.uploaded ? text.changesUploaded : text.changes, counts);
}

}  // namespace annotationsync
