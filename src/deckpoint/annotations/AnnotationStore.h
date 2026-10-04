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
#include <vector>

#include "AnnotationGeometry.h"
#include "AnnotationList.h"

class Epub;

namespace deckpoint::annotations {

class AnnotationStore {
 public:
  static constexpr const char* DIR = "/.crosspoint/annotations";

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
  bool save();

  // "YYYY-MM-DD HH:MM:SS" local time from the system clock, or
  // UNSET_TIMESTAMP when the clock has never been set (no RTC, no sync yet).
  static void now(char (&out)[20]);

 private:
  std::string path() const;
  bool load();

  std::string docId;
  AnnotationList list;
};

}  // namespace deckpoint::annotations
