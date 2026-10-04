#include "AnnotationStore.h"

#include <ChapterXPathResolver.h>
#include <Epub.h>
#include <HalStorage.h>
#include <KOReaderDocumentId.h>
#include <Logging.h>
#include <Memory.h>
#include <TrustedTime.h>

#include <ctime>

#include "AnnotationJson.h"

namespace deckpoint::annotations {

namespace {

// SD transfer chunk for load and save (heap, transient).
constexpr size_t IO_CHUNK = 1024;
// Bytes per entry in a typical AnnotationSync file, for the vector reserve.
constexpr size_t TYPICAL_ENTRY_BYTES = 450;

const char* addResultName(const AddResult r) {
  switch (r) {
    case AddResult::Added:
      return "added";
    case AddResult::Replaced:
      return "replaced";
    case AddResult::ReadOnly:
      return "read-only";
    case AddResult::TooMany:
      return "too many";
    case AddResult::TooLong:
      return "too long";
    case AddResult::OverBudget:
      return "over RAM budget";
    case AddResult::NotPlaceable:
      return "no position";
  }
  return "?";
}

struct FileSink {
  HalFile* file;
  char* buf;
  size_t used;
  bool ok;

  bool flush() {
    if (ok && used > 0) ok = file->write(buf, used) == used;
    used = 0;
    return ok;
  }
  static bool write(void* ctx, const char* data, size_t len) {
    auto* self = static_cast<FileSink*>(ctx);
    while (len > 0 && self->ok) {
      const size_t room = IO_CHUNK - self->used;
      const size_t n = len < room ? len : room;
      memcpy(self->buf + self->used, data, n);
      self->used += n;
      data += n;
      len -= n;
      if (self->used == IO_CHUNK) self->flush();
    }
    return self->ok;
  }
};

std::optional<uint32_t> resolve(const std::shared_ptr<Epub>& epub, const int spineIndex, const std::string& xpointer) {
  auto offset = ChapterXPathResolver::findVisibleTextOffsetForXPath(epub, spineIndex, xpointer);
  // Producers that omit an unindexed wrapper (same retry as ProgressMapper).
  if (!offset) offset = ChapterXPathResolver::findVisibleTextOffsetForXPath(epub, spineIndex, xpointer, true);
  return offset;
}

}  // namespace

std::unique_ptr<AnnotationStore> AnnotationStore::open(const std::string& bookPath) {
  std::string id = KOReaderDocumentId::calculate(bookPath);
  if (id.empty()) {
    LOG_ERR("ANN", "Cannot hash %s; annotations off", bookPath.c_str());
    return nullptr;
  }
  auto store = makeUniqueNoThrow<AnnotationStore>(std::move(id));
  if (!store) {
    LOG_ERR("ANN", "OOM: AnnotationStore");
    return nullptr;
  }
  store->load();
  return store;
}

std::string AnnotationStore::path() const { return std::string(DIR) + "/" + docId + ".json"; }

bool AnnotationStore::load() {
  const std::string file = path();
  if (!Storage.exists(file.c_str())) return false;  // no annotations yet: not an error
  HalFile f;
  if (!Storage.openFileForRead("ANN", file, f)) {
    list.markReadOnly();  // present but unreadable: never overwrite it
    return false;
  }
  const size_t size = f.fileSize();
  list.reserve(size / TYPICAL_ENTRY_BYTES + 1);

  // Parser (~2.1 KB) + chunk buffer, freed on return.
  auto reader = makeUniqueNoThrow<AnnotationJsonReader>(list);
  auto chunk = makeUniqueNoThrow<char[]>(IO_CHUNK);
  if (!reader || !chunk || !reader->begin()) {
    LOG_ERR("ANN", "OOM loading annotations");
    list.markReadOnly();
    return false;
  }
  const auto t0 = millis();
  int n;
  while ((n = f.read(chunk.get(), IO_CHUNK)) > 0) reader->feed(chunk.get(), static_cast<size_t>(n));
  const LoadReport report = reader->finish();

  LOG_INF("ANN", "%s: %u annotations (%u bytes, %u B strings) in %lums", docId.c_str(),
          static_cast<unsigned>(list.size()), static_cast<unsigned>(size), static_cast<unsigned>(list.blobBytes()),
          millis() - t0);
  if (report.unknownFields > 0) LOG_DBG("ANN", "%u unknown fields dropped on rewrite", (unsigned)report.unknownFields);
  if (report.keyMismatches > 0) LOG_DBG("ANN", "%u keys rewritten as pos0||pos1", (unsigned)report.keyMismatches);
  if (report.lossy) {
    LOG_ERR("ANN", "%s is %s (%u entries not kept: %s); read-only, file left as is", file.c_str(),
            report.parseError ? "malformed" : "over limits", static_cast<unsigned>(report.skipped),
            addResultName(report.firstFailure));
  }
  return !report.lossy;
}

bool AnnotationStore::save() {
  if (list.readOnly()) {
    LOG_ERR("ANN", "Not saving %s: loaded incompletely", docId.c_str());
    return false;
  }
  Storage.mkdir(DIR);
  const std::string file = path();
  const std::string tmp = file + ".tmp";
  auto buf = makeUniqueNoThrow<char[]>(IO_CHUNK);
  if (!buf) {
    LOG_ERR("ANN", "OOM: save buffer");
    return false;
  }
  {
    HalFile f;
    if (!Storage.openFileForWrite("ANN", tmp, f)) return false;
    FileSink sink{&f, buf.get(), 0, true};
    const bool written = writeAnnotationMap(list, JsonSink{&sink, &FileSink::write}) && sink.flush();
    if (!written) {
      LOG_ERR("ANN", "Failed to write %s", tmp.c_str());
      f.close();  // before remove
      Storage.remove(tmp.c_str());
      return false;
    }
  }
  if (!Storage.replaceFile(tmp.c_str(), file.c_str())) {
    LOG_ERR("ANN", "Failed to replace %s", file.c_str());
    return false;
  }
  LOG_DBG("ANN", "Saved %u annotations to %s", static_cast<unsigned>(list.size()), file.c_str());
  return true;
}

AddResult AnnotationStore::addAndSave(Annotation&& annotation) {
  const AddResult result = list.add(std::move(annotation));
  if (result != AddResult::Added && result != AddResult::Replaced) {
    LOG_ERR("ANN", "Annotation not added: %s", addResultName(result));
    return result;
  }
  save();
  return result;
}

AddResult AnnotationStore::addPlacedAndSave(Annotation&& annotation, const int spineIndex, const uint32_t startOffset,
                                            const uint32_t endOffset) {
  const std::string key = annotationKey(annotation);
  const AddResult result = list.add(std::move(annotation));
  if (result != AddResult::Added && result != AddResult::Replaced) {
    LOG_ERR("ANN", "Annotation not added: %s", addResultName(result));
    return result;
  }
  const int index = list.find(key);
  if (index >= 0) {
    Annotation& a = list[static_cast<size_t>(index)];
    if (a.spineIndex == spineIndex && endOffset > startOffset) {
      a.startOffset = startOffset;
      a.endOffset = endOffset;
      a.placement = Placement::Resolved;
    }
  }
  save();
  return result;
}

int AnnotationStore::highlightAt(const int spineIndex, const uint32_t offset) const {
  for (size_t i = list.size(); i-- > 0;) {
    const Annotation& a = list[i];
    if (a.spineIndex != spineIndex || a.placement != Placement::Resolved || a.deleted) continue;
    if (offset >= a.startOffset && offset < a.endOffset) return static_cast<int>(i);
  }
  return -1;
}

bool AnnotationStore::deleteAndSave(const size_t index) {
  char stamp[20];
  now(stamp);
  if (!list.tombstone(index, stamp)) {
    LOG_ERR("ANN", "Cannot delete annotation %u", static_cast<unsigned>(index));
    return false;
  }
  return save();
}

AddResult AnnotationStore::setNoteAndSave(const size_t index, const std::string_view note) {
  char stamp[20];
  now(stamp);
  const AddResult result = list.setNote(index, note, stamp);
  if (result != AddResult::Replaced) {
    LOG_ERR("ANN", "Note not set on %u: %s", static_cast<unsigned>(index), addResultName(result));
    return result;
  }
  return save() ? result : AddResult::ReadOnly;
}

void AnnotationStore::placeChapter(const std::shared_ptr<Epub>& epub, const int spineIndex) {
  if (!epub) return;
  const auto t0 = millis();
  unsigned placed = 0;
  unsigned failed = 0;
  for (size_t i = 0; i < list.size(); i++) {
    Annotation& a = list[i];
    if (a.spineIndex != spineIndex || a.placement != Placement::Unresolved || a.deleted || !a.isHighlight()) continue;
    // Copies: the resolver takes std::string; the block may move on edits.
    const std::string pos0(a.get(Field::Pos0));
    const std::string pos1(a.get(Field::Pos1));
    const auto start = resolve(epub, spineIndex, pos0);
    std::optional<uint32_t> end;
    if (start) {
      // A highlight running into a later chapter is drawn to this one's end.
      end =
          spineFromXPointer(pos1) == spineIndex ? resolve(epub, spineIndex, pos1) : std::optional<uint32_t>(UINT32_MAX);
    }
    if (start && end && *end > *start) {
      a.startOffset = *start;
      a.endOffset = *end;
      a.placement = Placement::Resolved;
      placed++;
    } else {
      a.placement = Placement::Failed;
      failed++;
      LOG_INF("ANN", "Unresolved in spine %d, kept: %s", spineIndex, pos0.c_str());
    }
  }
  if (placed + failed > 0) {
    LOG_DBG("ANN", "Spine %d: placed %u, unresolved %u in %lums", spineIndex, placed, failed, millis() - t0);
  }
}

void AnnotationStore::rangesFor(const int spineIndex, std::vector<HighlightRange>& out) const {
  out.clear();
  for (size_t i = 0; i < list.size(); i++) {
    const Annotation& a = list[i];
    if (a.spineIndex != spineIndex || a.placement != Placement::Resolved || a.deleted) continue;
    out.push_back({a.startOffset, a.endOffset, a.has(Field::Note)});
  }
}

void AnnotationStore::now(char (&out)[20]) {
  const int64_t epoch = trustedtime::trustedNow();
  if (epoch <= 0) {
    memcpy(out, UNSET_TIMESTAMP, sizeof(out));
    return;
  }
  const auto t = static_cast<time_t>(epoch);
  std::tm local{};
  localtime_r(&t, &local);  // TZ from Settings > Clock (HalClock::setTimezone)
  formatTimestamp(local, out);
}

}  // namespace deckpoint::annotations
