#pragma once

// DECKPOINT: a book's annotations on the SD card. The file is
// /.crosspoint/annotations/<partial MD5>.json (KOReader / AnnotationSync book
// id), outside the per-book cache directory so "clear cache" keeps it, in
// AnnotationSync's remote shape so sync can upload it as is.
//
// Placement (XPointer -> visible offsets) is resolved lazily, one chapter at a
// time, the first time a page of that chapter is drawn; entries that do not
// resolve stay in the file untouched (never tombstoned: AnnotationSync would
// propagate that as a delete).

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "AnnotationGeometry.h"
#include "AnnotationList.h"
#include "AnnotationMerge.h"

class Epub;

namespace deckpoint::annotations {

struct JsonSink;

class AnnotationStore {
 public:
  static constexpr const char* DIR = "/.crosspoint/annotations";
  // Per-book files in DIR, named <docId><suffix>:
  static constexpr const char* MAIN_SUFFIX = ".json";            // the annotations (AnnotationSync shape)
  static constexpr const char* FLAGS_SUFFIX = ".local.json";     // local-only flags (undated, seeds)
  static constexpr const char* SNAPSHOT_SUFFIX = ".sync.json";   // what we last uploaded
  static constexpr const char* BACKUP_SUFFIX = ".remote.bak";    // remote copy before a risky upload
  static constexpr const char* DOWNLOAD_SUFFIX = ".remote.tmp";  // sync: the GET body
  static constexpr const char* UPLOAD_SUFFIX = ".upload.tmp";    // sync: the PUT body
  static std::string filePath(const std::string& docId, const char* suffix);

  // Computes the book id and loads its file if there is one. Null on OOM or
  // when the book cannot be hashed.
  static std::unique_ptr<AnnotationStore> open(const std::string& bookPath);
  explicit AnnotationStore(std::string docId) : docId(std::move(docId)) {}

  const std::string& documentId() const { return docId; }
  const AnnotationList& annotations() const { return list; }
  bool readOnly() const { return list.readOnly(); }

  // Cheap check before any per-page work.
  bool hasHighlightsIn(int spineIndex) const { return list.hasHighlightsIn(spineIndex); }
  // Resolves this chapter's not-yet-placed highlights (streams the chapter's
  // XHTML twice per highlight; done once per chapter per session).
  void placeChapter(const std::shared_ptr<Epub>& epub, int spineIndex);
  // Placed, live highlights of the chapter (after placeChapter).
  void rangesFor(int spineIndex, std::vector<HighlightRange>& out) const;

  // Adds (or replaces, same pos0||pos1) and saves. The result says why not.
  AddResult addAndSave(Annotation&& annotation);
  // As addAndSave, for a highlight made on a laid-out page: its placement in
  // spineIndex is already known, so it is drawn without resolving its XPointers.
  AddResult addPlacedAndSave(Annotation&& annotation, int spineIndex, uint32_t startOffset, uint32_t endOffset);
  // List index of the placed, live highlight of this chapter that covers
  // `offset` (the most recent one when several do), or -1.
  int highlightAt(int spineIndex, uint32_t offset) const;
  // Tombstones entry `index` (deleted, datetime_updated = now) and saves.
  bool deleteAndSave(size_t index);
  // Sets entry `index`'s note (empty: removes it), datetime_updated = now, and
  // saves. Replaced on success; otherwise why not (see AnnotationList::setNote).
  AddResult setNoteAndSave(size_t index, std::string_view note);
  bool save();

  // "YYYY-MM-DD HH:MM:SS" local time from the system clock, or
  // UNSET_TIMESTAMP when the clock has never been set (no RTC, no sync yet).
  // Entries stamped while trustedtime::isCurrent() is false are kept undated.
  static void now(char (&out)[20]);

  // --- sync (round 2b) ---
  // Merges a downloaded remote map (empty list for a 404) into this book:
  // reads the snapshot, runs mergeAnnotations with now() / isCurrent(), and on
  // Ok replaces the list and saves it when it changed. `remote` is consumed on
  // Ok. Make the clock current first (rule 3). Refuses (status != Ok) when the
  // local list or the snapshot cannot be read completely.
  MergeReport mergeRemote(AnnotationList& remote);
  // The upload body: the list in AnnotationSync shape, seeds left out.
  bool writeUpload(const JsonSink& sink) const;
  // Records the upload as the snapshot; call after a successful PUT (or when
  // the merge reported !remoteChanged).
  bool saveSyncSnapshot() const;
  // Keeps the remote JSON as it was before we changed it (rule 7): before the
  // first upload of a book (no snapshot yet) and when the merge reports
  // removesOrBlanks. From a buffer or from a downloaded file.
  bool hasSyncSnapshot() const;
  static bool backupRemote(const std::string& docId, const char* data, size_t len);
  static bool backupRemoteFile(const std::string& docId, const std::string& downloadedPath);
  // Writes the upload body (writeUpload) to `file`.
  bool writeUploadFile(const std::string& file) const;
  // Streams an AnnotationSync map file (a downloaded remote) into `out`. False
  // when the file cannot be opened or on OOM; a lossy read leaves
  // out.readOnly() set (mergeAnnotations then refuses it).
  static bool readMapFile(const std::string& file, AnnotationList& out);
  // Dev cleanup (CMD:ANNOTATIONS_WIPE): removes every file of the book (main,
  // flags, snapshot, backup, sync temporaries). Returns how many existed.
  static unsigned removeAllFiles(const std::string& docId);

 private:
  std::string path() const;
  bool load();
  void loadFlags();
  bool saveFlags();
  bool loadSnapshotKeys(std::vector<uint64_t>& out) const;

  std::string docId;
  AnnotationList list;
};

}  // namespace deckpoint::annotations
