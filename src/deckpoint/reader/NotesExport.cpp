#include "NotesExport.h"

#include <Epub.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "deckpoint/annotations/AnnotationExport.h"
#include "deckpoint/annotations/AnnotationStore.h"

namespace deckpoint::reader {

namespace {

using annotations::Annotation;
using annotations::AnnotationStore;
using annotations::EntrySource;
using annotations::ExportBook;
using annotations::ExportEntry;
using annotations::Field;
using annotations::JsonSink;
using annotations::Placement;

// SD transfer chunk (heap, transient), shared by the read and write sides.
constexpr size_t IO_CHUNK = 1024;
// Book bytes per Kindle-style location.
constexpr size_t BYTES_PER_LOCATION = 128;

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

// Feeds the export writers from the store in book order.
struct Entries {
  const Epub* epub;
  const AnnotationStore* store;
  const std::vector<uint16_t>* order;
  size_t bookSize;
  // TOC lookups are SD reads: cached per spine (entries come in spine order).
  int cachedSpine = -2;
  int cachedToc = -1;
  std::string tocTitle;

  static bool get(void* ctx, const size_t i, ExportEntry& out) {
    auto* self = static_cast<Entries*>(ctx);
    const Annotation& a = self->store->annotations()[(*self->order)[i]];
    out.text = a.get(Field::Text);
    out.note = a.get(Field::Note);
    out.datetime = a.get(Field::Datetime);
    out.page = a.hasPageno ? a.pageno : 0;
    const int spine = a.spineIndex;
    if (spine < 0 || spine >= self->epub->getSpineItemsCount()) {
      out.chapter = a.get(Field::Chapter);
      return true;
    }
    if (spine != self->cachedSpine) {
      self->cachedSpine = spine;
      self->cachedToc = self->epub->getTocIndexForSpineIndex(spine);
      self->tocTitle = self->cachedToc >= 0 ? self->epub->getTocItem(self->cachedToc).title : std::string();
    }
    out.chapter = a.has(Field::Chapter) ? a.get(Field::Chapter) : std::string_view(self->tocTitle);
    // No chapter number: TOC position counts front matter ("Ch. 10" for a Prologue),
    // and the ## heading already names the chapter.
    out.chapterNumber = 0;
    const size_t before = spine > 0 ? self->epub->getCumulativeSpineItemSize(spine - 1) : 0;
    const size_t chapterBytes = self->epub->getCumulativeSpineItemSize(spine) - before;
    const size_t within = a.placement == Placement::Resolved ? std::min<size_t>(a.startOffset, chapterBytes) : 0;
    if (self->bookSize > 0) {
      out.percent = static_cast<int>(std::min<size_t>(100, (before + within) * 100 / self->bookSize));
    }
    out.location = static_cast<uint32_t>((before + within) / BYTES_PER_LOCATION + 1);
    return true;
  }
};

// Writes `tmp` through `body`, then replaces `path` with it.
template <typename Body>
bool writeReplacing(const std::string& path, char* buf, Body&& body) {
  const std::string tmp = path + ".tmp";
  {
    HalFile f;
    if (!Storage.openFileForWrite("NTX", tmp, f)) return false;
    FileSink sink{&f, buf, 0, true};
    const bool written = body(JsonSink{&sink, &FileSink::write}) && sink.flush();
    if (!written) {
      LOG_ERR("NTX", "Failed to write %s", tmp.c_str());
      f.close();  // before remove
      Storage.remove(tmp.c_str());
      return false;
    }
  }
  if (!Storage.replaceFile(tmp.c_str(), path.c_str())) {
    LOG_ERR("NTX", "Failed to replace %s", path.c_str());
    return false;
  }
  return true;
}

}  // namespace

bool exportBookNotes(const std::shared_ptr<Epub>& epub, const AnnotationStore& store, char* msg, const size_t msgSize) {
  std::vector<uint16_t> order;
  annotations::browseOrder(store.annotations(), order);
  if (order.empty()) {
    snprintf(msg, msgSize, "%s", tr(STR_NOTES_EMPTY));
    return false;
  }
  const auto fail = [&] {
    snprintf(msg, msgSize, "%s", tr(STR_NOTES_EXPORT_FAILED));
    return false;
  };
  // Write buffer + read chunk (2 KB), the Kindle title line and the clippings
  // filter (~0.3 KB each), freed on return.
  auto buf = makeUniqueNoThrow<char[]>(IO_CHUNK);
  auto chunk = makeUniqueNoThrow<char[]>(IO_CHUNK);
  auto titleLine = makeUniqueNoThrow<char[]>(annotations::CLIPPINGS_TITLE_MAX + 1);
  if (!buf || !chunk || !titleLine) {
    LOG_ERR("NTX", "OOM: export buffers");
    return fail();
  }

  char exported[20];
  AnnotationStore::now(exported);
  const ExportBook book{epub->getTitle(), epub->getAuthor(), store.documentId(), exported};
  Entries entries{epub.get(), &store, &order, epub->getBookSize()};
  const EntrySource source{&entries, order.size(), &Entries::get};

  Storage.mkdir(NOTES_EXPORT_DIR);  // creates /DeckPoint too
  char name[annotations::MAX_FILE_NAME_BYTES + 1];
  annotations::sanitizeFileName(book.title, name, sizeof(name));
  const std::string mdPath = std::string(NOTES_EXPORT_DIR) + "/" + name + ".md";
  if (!writeReplacing(mdPath, buf.get(),
                      [&](const JsonSink& sink) { return annotations::writeMarkdown(book, source, sink); })) {
    return fail();
  }

  const std::string_view title(
      titleLine.get(), annotations::clippingsTitleLine(book, titleLine.get(), annotations::CLIPPINGS_TITLE_MAX + 1));
  size_t replaced = 0;
  const bool clipped = writeReplacing(CLIPPINGS_PATH, buf.get(), [&](const JsonSink& sink) {
    if (Storage.exists(CLIPPINGS_PATH)) {
      HalFile in;
      if (!Storage.openFileForRead("NTX", CLIPPINGS_PATH, in)) return false;  // never drop what we cannot read
      auto filter = makeUniqueNoThrow<annotations::ClippingsFilter>(title, sink);
      if (!filter) {
        LOG_ERR("NTX", "OOM: clippings filter");
        return false;
      }
      int n;
      while ((n = in.read(chunk.get(), IO_CHUNK)) > 0) {
        if (!filter->feed(chunk.get(), static_cast<size_t>(n))) return false;
      }
      if (n < 0 || !filter->finish()) return false;
      replaced = filter->droppedBlocks();
    }
    return annotations::writeClippings(title, source, sink);
  });
  if (!clipped) return fail();

  LOG_INF("NTX", "Exported %u highlights to %s; %s: %u old blocks replaced", static_cast<unsigned>(order.size()),
          mdPath.c_str(), CLIPPINGS_PATH, static_cast<unsigned>(replaced));
  snprintf(msg, msgSize, "%s %u: %s", tr(STR_NOTES_EXPORTED), static_cast<unsigned>(order.size()), mdPath.c_str());
  return true;
}

}  // namespace deckpoint::reader
