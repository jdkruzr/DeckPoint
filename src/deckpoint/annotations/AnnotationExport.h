#pragma once

// DECKPOINT: pure pieces of the notes list and `:export` (host-tested):
// book-order sorting, FAT-safe file names, the Obsidian-friendly Markdown
// file, Kindle "My Clippings.txt" blocks, and the streaming filter that drops
// one book's old blocks from an existing clippings file. All output goes
// through a byte sink, so nothing here holds a whole file in RAM.

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "AnnotationJson.h"
#include "AnnotationList.h"

namespace deckpoint::annotations {

// Document order of two XPointers ("/body/DocFragment[3]/body/div/p[12]/text().45"):
// segment by segment, numeric indices numerically ("p" == "p[1]"), a trailing
// ".N" text offset last. Siblings with different element names cannot be
// ordered from the path alone; they compare by name, which keeps this a total
// order. <0, 0, >0 like strcmp.
int compareXPointers(std::string_view a, std::string_view b);

// List indices of the live highlights (not deleted, pos0 + pos1 set) in book
// order: spine, then pos0 (compareXPointers). Non-EPUB entries go last.
void browseOrder(const AnnotationList& list, std::vector<uint16_t>& out);

// A FAT/exFAT-safe file name (no extension) for `name`: / \ : * ? " < > | and
// control characters become spaces, space runs collapse, leading / trailing
// spaces and dots go, and the result is cut to MAX_FILE_NAME_BYTES on a UTF-8
// boundary. Empty -> "Untitled". Returns the length written (out is
// NUL-terminated; outSize must be > MAX_FILE_NAME_BYTES).
constexpr size_t MAX_FILE_NAME_BYTES = 80;
size_t sanitizeFileName(std::string_view name, char* out, size_t outSize);

struct ExportBook {
  std::string_view title;
  std::string_view author;
  std::string_view documentId;  // KOReader partial-MD5 book id
  std::string_view exported;    // "YYYY-MM-DD HH:MM:SS"; UNSET_TIMESTAMP / empty: left out
};

// One highlight as exported. Views stay valid until the next get() call.
struct ExportEntry {
  std::string_view text;
  std::string_view note;
  std::string_view chapter;   // grouping heading; empty: "Chapter N"
  std::string_view datetime;  // "YYYY-MM-DD HH:MM:SS"
  int chapterNumber = 0;      // 1-based; 0 unknown
  int percent = -1;           // position in the book, 0-100; -1 unknown
  uint32_t location = 0;      // Kindle-style location; 0 unknown
  int32_t page = 0;           // KOReader page number; 0 unknown
};

struct EntrySource {
  void* ctx;
  size_t count;
  bool (*get)(void* ctx, size_t index, ExportEntry& out);
};

// The Markdown notes file: YAML front matter (title, author, exported,
// koreader_id, highlights), "# Title", then per chapter "## Chapter" and per
// highlight a blockquote, its note as plain lines and a locator line
// "*— Ch. N, P% · date*". Leading Markdown markers (# > - + * = | ` ~ and
// "1." / "1)") are backslash-escaped so a quoted line stays a quote line.
bool writeMarkdown(const ExportBook& book, const EntrySource& entries, const JsonSink& sink);

// Kindle "Title (Author)" header line (without line end), cut to
// CLIPPINGS_TITLE_MAX bytes on a UTF-8 boundary so ClippingsFilter can always
// recognise it. Returns its length; out is NUL-terminated.
constexpr size_t CLIPPINGS_TITLE_MAX = 255;
size_t clippingsTitleLine(const ExportBook& book, char* out, size_t outSize);

// "Sunday, October 4, 2026 2:50:02 AM" (Kindle's US English) from
// "YYYY-MM-DD HH:MM:SS". False (out empty) when unparsable or unset.
bool formatKindleDate(std::string_view datetime, char* out, size_t outSize);

// Kindle blocks for every entry under `titleLine` (clippingsTitleLine), CRLF
// as Kindles write them:
//   Title (Author)
//   - Your Highlight on page 12 | Location 345 | Added on Sunday, ...
//   <blank>
//   text
//   ==========
// plus a "- Your Note on ..." block after a highlight that has a note. The
// page part is left out when unknown, the date part when unset.
bool writeClippings(std::string_view titleLine, const EntrySource& entries, const JsonSink& sink);

// Copies a My Clippings stream to `sink` minus the blocks whose header line is
// `dropTitle` (a leading BOM and a trailing CR are ignored when comparing).
// Blocks end at a "==========" line. Fed in arbitrary chunks; holds one
// header line (HEADER_MAX bytes) at most.
class ClippingsFilter {
 public:
  static constexpr size_t HEADER_MAX = CLIPPINGS_TITLE_MAX + 8;

  ClippingsFilter(std::string_view dropTitle, const JsonSink& sink) : dropTitle(dropTitle), sink(sink) {}
  bool feed(const char* data, size_t len);
  // Flushes an unterminated last line. False when the sink failed.
  bool finish();
  size_t droppedBlocks() const { return dropped; }

 private:
  enum class State : uint8_t { Header, Keep, Drop };
  bool emit(const char* data, size_t len);
  bool endHeaderLine(bool newline);
  bool headerIsSeparator() const;

  std::string_view dropTitle;
  JsonSink sink;
  State state = State::Header;
  char header[HEADER_MAX];
  size_t headerLen = 0;
  // Separator detection for the line being passed through.
  uint8_t sepCount = 0;
  bool sepPossible = true;
  bool sepCr = false;
  bool ok = true;
  size_t dropped = 0;
};

}  // namespace deckpoint::annotations
