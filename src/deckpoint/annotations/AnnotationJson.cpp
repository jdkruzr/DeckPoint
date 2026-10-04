#include "AnnotationJson.h"

#include <StreamingJsonParser.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace deckpoint::annotations {

namespace {
// Entry fields outside Annotation's string block (AnnotationJsonReader::field).
constexpr int FIELD_PAGENO = -2;
constexpr int FIELD_DELETED = -3;
}  // namespace

// --- reader ------------------------------------------------------------------

struct AnnotationJsonReader::Callbacks {
  static AnnotationJsonReader& self(void* ctx) { return *static_cast<AnnotationJsonReader*>(ctx); }
  static void key(void* ctx, const char* s, size_t n) { self(ctx).onKey(s, n); }
  static void string(void* ctx, const char* s, size_t n) { self(ctx).onString(s, n); }
  static void number(void* ctx, const char* s, size_t n) { self(ctx).onNumber(s, n); }
  static void boolean(void* ctx, bool v) { self(ctx).onBool(v); }
  static void null(void* ctx) { self(ctx).onNull(); }
  static void objectStart(void* ctx) { self(ctx).onContainerStart(true); }
  static void arrayStart(void* ctx) { self(ctx).onContainerStart(false); }
  static void end(void* ctx) { self(ctx).onContainerEnd(); }
};

AnnotationJsonReader::AnnotationJsonReader(AnnotationList& list) : list(list) {}
AnnotationJsonReader::~AnnotationJsonReader() = default;

bool AnnotationJsonReader::begin() {
  const JsonCallbacks cb{this,
                         &Callbacks::key,
                         &Callbacks::string,
                         &Callbacks::number,
                         &Callbacks::boolean,
                         &Callbacks::null,
                         &Callbacks::objectStart,
                         &Callbacks::end,
                         &Callbacks::arrayStart,
                         &Callbacks::end};
  parser.reset(new (std::nothrow) StreamingJsonParser(cb));
  return parser != nullptr;
}

void AnnotationJsonReader::feed(const char* data, const size_t len) {
  if (parser && !parser->hasError()) parser->feed(data, len);
}

LoadReport AnnotationJsonReader::finish() {
  if (!parser || parser->hasError() || depth != 0) report.parseError = true;
  if (report.parseError) report.lossy = true;
  if (report.lossy) list.markReadOnly();
  parser.reset();
  return report;
}

void AnnotationJsonReader::onContainerStart(const bool object) {
  // A value still pending in an entry is this container: an unknown nested
  // field, ignored.
  if (depth == 2) valuePending = false;
  depth++;
  if (depth == 1) {
    if (!object) report.parseError = true;  // the root must be a map
    return;
  }
  if (depth == 2) {
    if (!object) {
      // A root value that is not an annotation table.
      entryBroken = true;
      brokenReason = AddResult::NotPlaceable;
      return;
    }
    beginEntry();
    return;
  }
  if (depth == 3) report.unknownFields++;
}

void AnnotationJsonReader::onContainerEnd() {
  if (depth == 2) {
    if (valuePending) markBroken(AddResult::TooLong);  // last value overflowed the parser
    if (entryBroken) {
      skipEntry(brokenReason);
    } else {
      finishEntry();
    }
    entryBroken = false;
  }
  if (depth > 0) depth--;
}

void AnnotationJsonReader::onKey(const char* key, const size_t len) {
  if (depth == 1) {
    entryKey.assign(key, len);
    rootKeyFresh = true;
    return;
  }
  if (depth != 2) return;
  if (valuePending) markBroken(AddResult::TooLong);  // the previous value overflowed the parser
  valuePending = true;
  Field f;
  const std::string_view name(key, len);
  if (fieldFromName(name, f)) {
    field = static_cast<int>(f);
  } else if (name == "pageno") {
    field = FIELD_PAGENO;
  } else if (name == "deleted") {
    field = FIELD_DELETED;
  } else {
    field = -1;
    report.unknownFields++;
  }
}

void AnnotationJsonReader::onString(const char* value, const size_t len) {
  if (depth == 1) {
    // A root value that is a bare string.
    skipEntry(AddResult::NotPlaceable);
    return;
  }
  if (depth != 2 || !valuePending) return;
  valuePending = false;
  if (field >= 0) {
    values[field].assign(value, len);
    if (static_cast<Field>(field) == Field::Page) pageIsNumber = false;
  }
}

void AnnotationJsonReader::onNumber(const char* value, const size_t len) {
  if (depth == 1) {
    skipEntry(AddResult::NotPlaceable);
    return;
  }
  if (depth != 2 || !valuePending) return;
  valuePending = false;
  if (field == FIELD_PAGENO) {
    pageno = static_cast<int32_t>(strtol(value, nullptr, 10));
    hasPageno = true;
  } else if (field == static_cast<int>(Field::Page)) {
    values[field].assign(value, len);
    pageIsNumber = true;
  } else if (field >= 0) {
    report.unknownFields++;  // a number where the model keeps a string
  }
}

void AnnotationJsonReader::onBool(const bool value) {
  if (depth == 1) {
    skipEntry(AddResult::NotPlaceable);
    return;
  }
  if (depth != 2 || !valuePending) return;
  valuePending = false;
  if (field == FIELD_DELETED) deleted = value;
}

void AnnotationJsonReader::onNull() {
  if (depth == 1) {
    skipEntry(AddResult::NotPlaceable);
    return;
  }
  if (depth == 2) valuePending = false;
}

void AnnotationJsonReader::beginEntry() {
  for (size_t i = 0; i < FIELD_COUNT; i++) {
    values[i].clear();
  }
  pageno = 0;
  hasPageno = false;
  pageIsNumber = false;
  deleted = false;
  entryBroken = false;
  if (!rootKeyFresh) markBroken(AddResult::TooLong);  // its key overflowed the parser
  rootKeyFresh = false;
  valuePending = false;
  field = -1;
}

void AnnotationJsonReader::finishEntry() {
  Annotation annotation;
  std::string_view views[FIELD_COUNT];
  for (size_t i = 0; i < FIELD_COUNT; i++) views[i] = values[i];
  if (!annotation.assign(views)) {
    skipEntry(AddResult::OverBudget);
    return;
  }
  annotation.pageno = pageno;
  annotation.hasPageno = hasPageno;
  annotation.pageIsNumber = pageIsNumber;
  annotation.deleted = deleted;
  if (annotationKey(annotation) != entryKey) report.keyMismatches++;
  const AddResult result = list.adopt(std::move(annotation));
  if (result == AddResult::Added || result == AddResult::Replaced) {
    report.loaded++;
  } else {
    skipEntry(result);
  }
}

void AnnotationJsonReader::markBroken(const AddResult why) {
  if (!entryBroken) brokenReason = why;
  entryBroken = true;
}

void AnnotationJsonReader::skipEntry(const AddResult why) {
  report.skipped++;
  report.lossy = true;
  if (report.firstFailure == AddResult::Added) report.firstFailure = why;
}

// --- writer ------------------------------------------------------------------

namespace {

class Out {
 public:
  explicit Out(const JsonSink& sink) : sink(sink) {}
  bool ok() const { return good; }
  void raw(const char* s, const size_t n) {
    if (good && n > 0) good = sink.write(sink.ctx, s, n);
  }
  void raw(const char* s) { raw(s, strlen(s)); }

  // JSON string literal: escapes quote, backslash, '/' and control characters;
  // other bytes (UTF-8) pass through.
  void string(const std::string_view s) {
    raw("\"", 1);
    body(s);
    raw("\"", 1);
  }

  // The inside of a string literal (no quotes).
  void body(const std::string_view s) {
    size_t runStart = 0;
    for (size_t i = 0; i < s.size(); i++) {
      const auto c = static_cast<unsigned char>(s[i]);
      const char* esc = nullptr;
      char hex[7];
      switch (c) {
        case '"':
          esc = "\\\"";
          break;
        case '\\':
          esc = "\\\\";
          break;
        case '/':
          esc = "\\/";
          break;
        case '\n':
          esc = "\\n";
          break;
        case '\r':
          esc = "\\r";
          break;
        case '\t':
          esc = "\\t";
          break;
        case '\b':
          esc = "\\b";
          break;
        case '\f':
          esc = "\\f";
          break;
        default:
          if (c < 0x20) {
            snprintf(hex, sizeof(hex), "\\u%04x", c);
            esc = hex;
          }
          break;
      }
      if (!esc) continue;
      raw(s.data() + runStart, i - runStart);
      raw(esc);
      runStart = i + 1;
    }
    raw(s.data() + runStart, s.size() - runStart);
  }

 private:
  const JsonSink& sink;
  bool good = true;
};

// Field order in the written object (alphabetical, like a sorted encoder).
enum class Slot : uint8_t { String, Deleted, Pageno };
struct WriteField {
  const char* name;
  Slot slot;
  Field field;
};
constexpr WriteField WRITE_ORDER[] = {
    {"chapter", Slot::String, Field::Chapter},   {"color", Slot::String, Field::Color},
    {"datetime", Slot::String, Field::Datetime}, {"datetime_updated", Slot::String, Field::DatetimeUpdated},
    {"deleted", Slot::Deleted, Field::COUNT},    {"drawer", Slot::String, Field::Drawer},
    {"note", Slot::String, Field::Note},         {"page", Slot::String, Field::Page},
    {"pageno", Slot::Pageno, Field::COUNT},      {"pos0", Slot::String, Field::Pos0},
    {"pos1", Slot::String, Field::Pos1},         {"text", Slot::String, Field::Text},
};

// The entry's map key, escaped piecewise (no concatenated copy).
void writeKey(Out& out, const Annotation& a) {
  if (a.isHighlight()) {
    out.body(a.get(Field::Pos0));
    out.raw("||", 2);
    out.body(a.get(Field::Pos1));
  } else {
    out.raw("BOOKMARK|", 9);
    out.body(a.get(Field::Page));
  }
}

}  // namespace

bool writeAnnotationMap(const AnnotationList& list, const JsonSink& sink, const bool includeLocalOnly) {
  Out out(sink);
  out.raw("{", 1);
  bool firstEntry = true;
  for (size_t i = 0; i < list.size() && out.ok(); i++) {
    const Annotation& a = list[i];
    if (a.localOnly && !includeLocalOnly) continue;
    out.raw(firstEntry ? "\"" : ",\"");
    firstEntry = false;
    writeKey(out, a);
    out.raw("\":{", 3);
    bool firstField = true;
    for (const auto& wf : WRITE_ORDER) {
      char number[16];
      switch (wf.slot) {
        case Slot::String:
          if (!a.has(wf.field)) continue;
          break;
        case Slot::Deleted:
          if (!a.deleted) continue;
          break;
        case Slot::Pageno:
          if (!a.hasPageno) continue;
          break;
      }
      if (!firstField) out.raw(",", 1);
      firstField = false;
      out.string(wf.name);
      out.raw(":", 1);
      switch (wf.slot) {
        case Slot::String:
          if (wf.field == Field::Page && a.pageIsNumber) {
            out.raw(a.get(Field::Page).data(), a.get(Field::Page).size());
          } else {
            out.string(a.get(wf.field));
          }
          break;
        case Slot::Deleted:
          out.raw("true", 4);
          break;
        case Slot::Pageno:
          snprintf(number, sizeof(number), "%ld", static_cast<long>(a.pageno));
          out.raw(number);
          break;
      }
    }
    out.raw("}", 1);
  }
  out.raw("}", 1);
  return out.ok();
}

// --- local flags sidecar -------------------------------------------------------

bool hasLocalFlags(const AnnotationList& list) {
  for (size_t i = 0; i < list.size(); i++) {
    if (list[i].undated || list[i].localOnly) return true;
  }
  return false;
}

bool writeLocalFlags(const AnnotationList& list, const JsonSink& sink) {
  Out out(sink);
  const auto array = [&](const char* name, bool Annotation::* flag) {
    out.raw(name);
    bool first = true;
    for (size_t i = 0; i < list.size() && out.ok(); i++) {
      if (!(list[i].*flag)) continue;
      out.raw(first ? "\"" : ",\"");
      first = false;
      writeKey(out, list[i]);
      out.raw("\"", 1);
    }
    out.raw("]");
  };
  array("{\"undated\":[", &Annotation::undated);
  array(",\"local_only\":[", &Annotation::localOnly);
  out.raw("}", 1);
  return out.ok();
}

struct LocalFlagsReader::Callbacks {
  static LocalFlagsReader& self(void* ctx) { return *static_cast<LocalFlagsReader*>(ctx); }
  static void key(void* ctx, const char* s, size_t n) {
    auto& r = self(ctx);
    if (r.depth != 1) return;
    const std::string_view name(s, n);
    r.flag = name == "undated" ? 1 : name == "local_only" ? 2 : 0;
  }
  static void string(void* ctx, const char* s, size_t n) {
    auto& r = self(ctx);
    if (r.depth != 2 || r.flag == 0) return;
    const int i = r.list.find(std::string_view(s, n));
    if (i < 0) return;
    Annotation& a = r.list[static_cast<size_t>(i)];
    (r.flag == 1 ? a.undated : a.localOnly) = true;
    r.applied++;
  }
  static void scalar(void*, const char*, size_t) {}
  static void boolean(void*, bool) {}
  static void null(void*) {}
  static void start(void* ctx) { self(ctx).depth++; }
  static void end(void* ctx) {
    auto& r = self(ctx);
    if (r.depth > 0) r.depth--;
  }
};

LocalFlagsReader::LocalFlagsReader(AnnotationList& list) : list(list) {}
LocalFlagsReader::~LocalFlagsReader() = default;

bool LocalFlagsReader::begin() {
  const JsonCallbacks cb{this,
                         &Callbacks::key,
                         &Callbacks::string,
                         &Callbacks::scalar,
                         &Callbacks::boolean,
                         &Callbacks::null,
                         &Callbacks::start,
                         &Callbacks::end,
                         &Callbacks::start,
                         &Callbacks::end};
  parser.reset(new (std::nothrow) StreamingJsonParser(cb));
  return parser != nullptr;
}

void LocalFlagsReader::feed(const char* data, const size_t len) {
  if (parser && !parser->hasError()) parser->feed(data, len);
}

bool LocalFlagsReader::finish(size_t* appliedOut) {
  const bool ok = parser && !parser->hasError() && depth == 0;
  parser.reset();
  if (appliedOut) *appliedOut = applied;
  return ok;
}

}  // namespace deckpoint::annotations
