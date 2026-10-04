#pragma once

// DECKPOINT: AnnotationSync's remote JSON shape (host-tested): one object keyed
// pos0 .. "||" .. pos1 (bookmarks: "BOOKMARK|" .. page) whose values are the
// KOReader annotation tables, e.g.
//   {"/body/DocFragment[10]/body/div/p[1]/text().0||/body/DocFragment[10]/body/div/p[1]/text().28":
//     {"chapter":"Prologue","color":"gray","datetime":"2026-10-04 02:50:02","drawer":"lighten",
//      "page":"/body/DocFragment[10]/body/div/p[1]/text().0","pageno":15,"pos0":"...","pos1":"...",
//      "text":"I would have lived in peace."}}
// Read as a stream (StreamingJsonParser, ~2.1 KB) in chunks, so the file never
// sits in RAM whole; written as a stream through a byte sink. Fields outside
// Annotation's model are dropped on rewrite.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "AnnotationList.h"

class StreamingJsonParser;

namespace deckpoint::annotations {

struct LoadReport {
  size_t loaded = 0;
  size_t skipped = 0;        // entries that could not be kept (see lossy)
  size_t unknownFields = 0;  // fields outside the model (dropped on rewrite)
  size_t keyMismatches = 0;  // map key differs from pos0||pos1 (rewritten canonical)
  bool parseError = false;   // malformed JSON
  bool lossy = false;        // something in the file is not in the list
  AddResult firstFailure = AddResult::Added;
};

class AnnotationJsonReader {
 public:
  explicit AnnotationJsonReader(AnnotationList& list);
  ~AnnotationJsonReader();

  // Allocates the parser. False on OOM.
  bool begin();
  void feed(const char* data, size_t len);
  // Marks the list read-only when the load was lossy or malformed.
  LoadReport finish();

 private:
  struct Callbacks;
  void onKey(const char* key, size_t len);
  void onString(const char* value, size_t len);
  void onNumber(const char* value, size_t len);
  void onBool(bool value);
  void onNull();
  void onContainerStart(bool object);
  void onContainerEnd();
  void beginEntry();
  void finishEntry();
  void markBroken(AddResult why);
  void skipEntry(AddResult why);

  AnnotationList& list;
  std::unique_ptr<StreamingJsonParser> parser;
  LoadReport report;

  // Entry under construction; the strings keep their capacity across entries.
  std::string values[FIELD_COUNT];
  std::string entryKey;
  int32_t pageno = 0;
  bool hasPageno = false;
  bool pageIsNumber = false;
  bool deleted = false;
  bool entryBroken = false;  // the entry lost a value: skipped, list read-only
  AddResult brokenReason = AddResult::Added;

  int depth = 0;
  bool rootKeyFresh = false;  // a root-level key arrived since the last entry
  bool valuePending = false;  // an entry field key arrived, its value has not
  int field = -1;             // pending entry field: Field index, or -1 unknown / -2 pageno / -3 deleted
};

// Byte sink for the writer; returns false to abort.
struct JsonSink {
  void* ctx;
  bool (*write)(void* ctx, const char* data, size_t len);
};

// Writes `list` in AnnotationSync's shape (tombstones included, '/' escaped as
// "\/" like KOReader's encoder). includeLocalOnly = false leaves out dev seeds
// (what is uploaded). False when the sink failed.
bool writeAnnotationMap(const AnnotationList& list, const JsonSink& sink, bool includeLocalOnly = true);

// DECKPOINT: the local-only flags sidecar (<md5>.local.json), keyed like the
// main file: {"undated":["<key>",...],"local_only":["<key>",...]}.
bool hasLocalFlags(const AnnotationList& list);
bool writeLocalFlags(const AnnotationList& list, const JsonSink& sink);

// Applies a sidecar to an already loaded list (keys it does not hold are
// ignored). Streamed like AnnotationJsonReader.
class LocalFlagsReader {
 public:
  explicit LocalFlagsReader(AnnotationList& list);
  ~LocalFlagsReader();
  bool begin();  // false on OOM
  void feed(const char* data, size_t len);
  // Entries flagged; false when the sidecar was malformed (flags read so far stay).
  bool finish(size_t* applied = nullptr);

 private:
  struct Callbacks;
  AnnotationList& list;
  std::unique_ptr<StreamingJsonParser> parser;
  int depth = 0;
  int flag = 0;  // 1 undated, 2 local_only, 0 other
  size_t applied = 0;
};

}  // namespace deckpoint::annotations
