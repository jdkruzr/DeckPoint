#include "AnnotationExport.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "AnnotationGeometry.h"

namespace deckpoint::annotations {

namespace {

constexpr std::string_view SEPARATOR = "==========";
constexpr std::string_view BOM = "\xEF\xBB\xBF";

bool isDigit(const char c) { return c >= '0' && c <= '9'; }

// --------------------------------------------------------------- XPointers

struct Segment {
  std::string_view name;
  uint32_t index = 1;
  bool hasOffset = false;
  uint32_t offset = 0;
};

uint32_t parseNumber(std::string_view s) {
  uint32_t n = 0;
  for (const char c : s) {
    if (!isDigit(c)) break;
    n = n > 0x0FFFFFFF ? n : n * 10 + static_cast<uint32_t>(c - '0');
  }
  return n;
}

// Next "/name[i]" segment of `rest` (consumed); the last one may end in ".N".
Segment nextSegment(std::string_view& rest) {
  if (!rest.empty() && rest.front() == '/') rest.remove_prefix(1);
  const size_t slash = rest.find('/');
  std::string_view seg = rest.substr(0, slash);
  rest = slash == std::string_view::npos ? std::string_view() : rest.substr(slash);
  Segment out;
  if (rest.empty()) {
    // "text().45": a trailing run of digits after the last '.' is the offset.
    const size_t dot = seg.rfind('.');
    if (dot != std::string_view::npos && dot + 1 < seg.size() &&
        std::all_of(seg.begin() + static_cast<std::ptrdiff_t>(dot) + 1, seg.end(), isDigit)) {
      out.hasOffset = true;
      out.offset = parseNumber(seg.substr(dot + 1));
      seg = seg.substr(0, dot);
    }
  }
  const size_t bracket = seg.find('[');
  out.name = seg.substr(0, bracket);
  if (bracket != std::string_view::npos) out.index = parseNumber(seg.substr(bracket + 1));
  return out;
}

// --------------------------------------------------------------- output

struct Out {
  const JsonSink& sink;
  bool ok = true;

  void put(const std::string_view s) {
    if (ok && !s.empty()) ok = sink.write(sink.ctx, s.data(), s.size());
  }
  void put(const char c) { put(std::string_view(&c, 1)); }
  template <typename... Args>
  void putf(const char* fmt, Args... args) {
    char buf[96];
    const int n = snprintf(buf, sizeof(buf), fmt, args...);
    if (n > 0) put(std::string_view(buf, std::min(static_cast<size_t>(n), sizeof(buf) - 1)));
  }
};

// Calls fn(line) for every line of `text` ('\n' splits, '\r' dropped).
template <typename Fn>
void forEachLine(std::string_view text, Fn&& fn) {
  while (true) {
    const size_t nl = text.find('\n');
    std::string_view line = text.substr(0, nl);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    fn(line);
    if (nl == std::string_view::npos) return;
    text.remove_prefix(nl + 1);
  }
}

std::string_view trimBlank(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\n' || s.front() == '\r')) {
    s.remove_prefix(1);
  }
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n' || s.back() == '\r')) {
    s.remove_suffix(1);
  }
  return s;
}

// One Markdown text line with its leading block marker escaped.
void putEscapedLine(Out& out, std::string_view line) {
  while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
  if (line.empty()) return;
  constexpr std::string_view MARKERS = "#>-+*=|`~";
  if (MARKERS.find(line.front()) != std::string_view::npos) {
    out.put('\\');
  } else if (isDigit(line.front())) {
    size_t i = 0;
    while (i < line.size() && isDigit(line[i])) i++;
    if (i < line.size() && (line[i] == '.' || line[i] == ')')) {
      out.put(line.substr(0, i));
      out.put('\\');
      line.remove_prefix(i);
    }
  }
  out.put(line);
}

// YAML double-quoted scalar.
void putYamlString(Out& out, const std::string_view s) {
  out.put('"');
  for (const char c : s) {
    if (c == '"' || c == '\\') {
      out.put('\\');
      out.put(c);
    } else if (static_cast<uint8_t>(c) < 0x20) {
      out.put(' ');
    } else {
      out.put(c);
    }
  }
  out.put('"');
}

// `s` on one line (line breaks become spaces).
void putOneLine(Out& out, const std::string_view s) {
  for (const char c : s) out.put(c == '\n' || c == '\r' ? ' ' : c);
}

bool dateIsSet(const std::string_view datetime) { return !datetime.empty() && datetime != UNSET_TIMESTAMP; }

// UTF-8 safe cut of s to at most maxBytes.
std::string_view cutUtf8(std::string_view s, const size_t maxBytes) {
  if (s.size() <= maxBytes) return s;
  size_t n = maxBytes;
  while (n > 0 && (static_cast<uint8_t>(s[n]) & 0xC0) == 0x80) n--;
  return s.substr(0, n);
}

// Length of out[0, n) without a code point cut short at its end.
size_t completeUtf8(const char* out, size_t n) {
  size_t lead = n;
  while (lead > 0 && (static_cast<uint8_t>(out[lead - 1]) & 0xC0) == 0x80) lead--;
  if (lead == 0) return n;
  const auto b = static_cast<uint8_t>(out[lead - 1]);
  const size_t need = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1;
  return n - (lead - 1) < need ? lead - 1 : n;
}

// Days since 1970-01-01 for a civil date (Hinnant's days_from_civil).
int64_t daysFromCivil(int y, const unsigned m, const unsigned d) {
  y -= m <= 2 ? 1 : 0;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

}  // namespace

int compareXPointers(std::string_view a, std::string_view b) {
  while (!a.empty() && !b.empty()) {
    const Segment sa = nextSegment(a);
    const Segment sb = nextSegment(b);
    if (const int c = sa.name.compare(sb.name); c != 0) return c < 0 ? -1 : 1;
    if (sa.index != sb.index) return sa.index < sb.index ? -1 : 1;
    if (sa.hasOffset != sb.hasOffset) return sa.hasOffset ? 1 : -1;  // the element before a point inside it
    if (sa.offset != sb.offset) return sa.offset < sb.offset ? -1 : 1;
  }
  if (a.empty() != b.empty()) return a.empty() ? -1 : 1;  // ancestor first
  return 0;
}

void browseOrder(const AnnotationList& list, std::vector<uint16_t>& out) {
  out.clear();
  size_t live = 0;
  for (size_t i = 0; i < list.size(); i++) {
    if (!list[i].deleted && list[i].isHighlight()) live++;
  }
  out.reserve(live);
  for (size_t i = 0; i < list.size(); i++) {
    if (!list[i].deleted && list[i].isHighlight()) out.push_back(static_cast<uint16_t>(i));
  }
  const auto before = [&list](const uint16_t x, const uint16_t y) {
    const Annotation& a = list[x];
    const Annotation& b = list[y];
    // -1 (not an EPUB XPointer) wraps to the largest value: last.
    const auto sa = static_cast<uint16_t>(a.spineIndex);
    const auto sb = static_cast<uint16_t>(b.spineIndex);
    if (sa != sb) return sa < sb;
    return compareXPointers(a.get(Field::Pos0), b.get(Field::Pos0)) < 0;
  };
  // Insertion sort: stable, no extra buffer, a fraction of std::sort's code;
  // highlights are mostly made in reading order, so it runs near-linear.
  for (size_t i = 1; i < out.size(); i++) {
    const uint16_t v = out[i];
    size_t j = i;
    while (j > 0 && before(v, out[j - 1])) {
      out[j] = out[j - 1];
      j--;
    }
    out[j] = v;
  }
}

size_t sanitizeFileName(const std::string_view name, char* out, const size_t outSize) {
  constexpr std::string_view FORBIDDEN = "/\\:*?\"<>|";
  const size_t cap = std::min(MAX_FILE_NAME_BYTES, outSize - 1);
  size_t n = 0;
  bool pendingSpace = false;
  for (const char c : name) {
    const bool blank =
        c == ' ' || static_cast<uint8_t>(c) < 0x20 || c == 0x7F || FORBIDDEN.find(c) != std::string_view::npos;
    if (blank) {
      pendingSpace = n > 0;
      continue;
    }
    if (n == 0 && c == '.') continue;  // no hidden / dot-leading names
    if (n + (pendingSpace ? 2 : 1) > cap) break;
    if (pendingSpace) out[n++] = ' ';
    pendingSpace = false;
    out[n++] = c;
  }
  n = completeUtf8(out, n);
  while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '.')) n--;
  if (n == 0) {
    constexpr std::string_view FALLBACK = "Untitled";
    n = std::min(FALLBACK.size(), outSize - 1);
    memcpy(out, FALLBACK.data(), n);
  }
  out[n] = '\0';
  return n;
}

bool writeMarkdown(const ExportBook& book, const EntrySource& entries, const JsonSink& sink) {
  Out out{sink};
  out.put("---\ntitle: ");
  putYamlString(out, book.title);
  out.put("\nauthor: ");
  putYamlString(out, book.author);
  if (dateIsSet(book.exported)) {
    out.put("\nexported: ");
    out.put(book.exported);
  }
  out.put("\nkoreader_id: ");
  out.put(book.documentId);
  out.putf("\nhighlights: %u\nsource: DeckPoint\n---\n\n# ", static_cast<unsigned>(entries.count));
  putOneLine(out, trimBlank(book.title));
  out.put('\n');
  if (!book.author.empty()) {
    out.put("\n*");
    out.put(book.author);
    out.put("*\n");
  }

  // Heading of the group being written (copied: the entry's views change).
  char lastChapter[128];
  size_t lastChapterLen = 0;
  int lastNumber = -1;
  bool firstGroup = true;
  ExportEntry e;
  for (size_t i = 0; i < entries.count && out.ok; i++) {
    e = ExportEntry{};
    if (!entries.get(entries.ctx, i, e)) continue;
    const std::string_view chapter = cutUtf8(trimBlank(e.chapter), sizeof(lastChapter));
    if (firstGroup || chapter != std::string_view(lastChapter, lastChapterLen) || e.chapterNumber != lastNumber) {
      firstGroup = false;
      memcpy(lastChapter, chapter.data(), chapter.size());
      lastChapterLen = chapter.size();
      lastNumber = e.chapterNumber;
      out.put("\n## ");
      if (!chapter.empty()) {
        putOneLine(out, chapter);
      } else if (e.chapterNumber > 0) {
        out.putf("Chapter %d", e.chapterNumber);
      } else {
        out.put("Highlights");
      }
      out.put('\n');
    }

    out.put('\n');
    forEachLine(trimBlank(e.text), [&](const std::string_view line) {
      out.put('>');
      if (!trimBlank(line).empty()) {
        out.put(' ');
        putEscapedLine(out, line);
      }
      out.put('\n');
    });
    const std::string_view note = trimBlank(e.note);
    if (!note.empty()) {
      out.put('\n');
      forEachLine(note, [&](const std::string_view line) {
        putEscapedLine(out, line);
        out.put('\n');
      });
    }
    // Locator: "*— Ch. 3, 37% · 2026-10-04 02:50*"
    out.put("\n*\xE2\x80\x94 ");
    bool first = true;
    if (e.chapterNumber > 0) {
      out.putf("Ch. %d", e.chapterNumber);
      first = false;
    }
    if (e.percent >= 0) {
      out.putf(first ? "%d%%" : ", %d%%", e.percent);
      first = false;
    }
    if (dateIsSet(e.datetime)) {
      if (!first) out.put(" \xC2\xB7 ");
      out.put(e.datetime.substr(0, 16));  // to the minute
    }
    out.put("*\n");
  }
  return out.ok;
}

size_t clippingsTitleLine(const ExportBook& book, char* out, const size_t outSize) {
  if (outSize == 0) return 0;
  const size_t cap = std::min(CLIPPINGS_TITLE_MAX, outSize - 1);
  size_t n = 0;
  const auto append = [&](const std::string_view s) {
    for (const char c : s) {
      if (n == cap) return;
      out[n++] = (c == '\n' || c == '\r') ? ' ' : c;
    }
  };
  const std::string_view title = trimBlank(book.title);
  const std::string_view author = trimBlank(book.author);
  append(title.empty() ? std::string_view("Untitled") : title);
  if (!author.empty()) {
    append(" (");
    append(author);
    append(")");
  }
  n = completeUtf8(out, n);
  out[n] = '\0';
  return n;
}

bool formatKindleDate(const std::string_view datetime, char* out, const size_t outSize) {
  if (outSize > 0) out[0] = '\0';
  if (!dateIsSet(datetime) || datetime.size() < 19) return false;
  for (const size_t i : {0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15, 17, 18}) {
    if (!isDigit(datetime[i])) return false;
  }
  const int year = static_cast<int>(parseNumber(datetime.substr(0, 4)));
  const unsigned month = parseNumber(datetime.substr(5, 2));
  const unsigned day = parseNumber(datetime.substr(8, 2));
  const unsigned hour = parseNumber(datetime.substr(11, 2));
  const unsigned minute = parseNumber(datetime.substr(14, 2));
  const unsigned second = parseNumber(datetime.substr(17, 2));
  if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 60) return false;
  static constexpr const char* WEEKDAYS[] = {"Thursday", "Friday",  "Saturday", "Sunday",
                                             "Monday",   "Tuesday", "Wednesday"};  // 1970-01-01 was a Thursday
  static constexpr const char* MONTHS[] = {"January", "February", "March",     "April",   "May",      "June",
                                           "July",    "August",   "September", "October", "November", "December"};
  const int64_t days = daysFromCivil(year, month, day);
  const auto weekday = static_cast<size_t>(((days % 7) + 7) % 7);
  const unsigned hour12 = hour % 12 == 0 ? 12 : hour % 12;
  snprintf(out, outSize, "%s, %s %u, %d %u:%02u:%02u %s", WEEKDAYS[weekday], MONTHS[month - 1], day, year, hour12,
           minute, second, hour < 12 ? "AM" : "PM");
  return true;
}

bool writeClippings(const std::string_view titleLine, const EntrySource& entries, const JsonSink& sink) {
  Out out{sink};
  char date[48];
  ExportEntry e;
  const auto block = [&](const char* kind, const std::string_view body) {
    out.put(titleLine);
    out.put("\r\n- Your ");
    out.put(kind);
    if (e.page > 0) {
      out.putf(" on page %ld | Location %lu", static_cast<long>(e.page), static_cast<unsigned long>(e.location));
    } else {
      out.putf(" on Location %lu", static_cast<unsigned long>(e.location));
    }
    if (formatKindleDate(e.datetime, date, sizeof(date))) {
      out.put(" | Added on ");
      out.put(date);
    }
    out.put("\r\n\r\n");
    forEachLine(body, [&](const std::string_view line) {
      out.put(line);
      // A body line must never read as the block separator.
      if (line == SEPARATOR) out.put(' ');
      out.put("\r\n");
    });
    out.put(SEPARATOR);
    out.put("\r\n");
  };
  for (size_t i = 0; i < entries.count && out.ok; i++) {
    e = ExportEntry{};
    if (!entries.get(entries.ctx, i, e)) continue;
    if (e.location == 0) e.location = 1;
    block("Highlight", trimBlank(e.text));
    const std::string_view note = trimBlank(e.note);
    if (!note.empty()) block("Note", note);
  }
  return out.ok;
}

// --------------------------------------------------------------- filter

bool ClippingsFilter::emit(const char* data, const size_t len) {
  if (ok && len > 0) ok = sink.write(sink.ctx, data, len);
  return ok;
}

bool ClippingsFilter::headerIsSeparator() const {
  std::string_view line(header, headerLen);
  if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
  return line == SEPARATOR;
}

bool ClippingsFilter::endHeaderLine(const bool newline) {
  std::string_view line(header, headerLen);
  if (line.substr(0, BOM.size()) == BOM) line.remove_prefix(BOM.size());
  if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
  if (line == dropTitle && !dropTitle.empty()) {
    state = State::Drop;
    dropped++;
  } else {
    // A separator right after a separator is an empty block: still between blocks.
    state = headerIsSeparator() ? State::Header : State::Keep;
    emit(header, headerLen);
    if (newline) emit("\n", 1);
  }
  headerLen = 0;
  sepCount = 0;
  sepPossible = true;
  sepCr = false;
  return ok;
}

bool ClippingsFilter::feed(const char* data, const size_t len) {
  size_t runStart = 0;  // start of the pass-through run in Keep
  for (size_t i = 0; i < len && ok; i++) {
    const char c = data[i];
    if (state == State::Header) {
      if (c == '\n') {
        endHeaderLine(true);
      } else if (headerLen < HEADER_MAX) {
        header[headerLen++] = c;
        continue;
      } else {
        // Longer than any title we write: not ours. Pass the line through.
        state = State::Keep;
        emit(header, headerLen);
        headerLen = 0;
        sepPossible = false;
        runStart = i;
        i--;  // this byte goes through the Keep path
        continue;
      }
      runStart = i + 1;
      continue;
    }
    if (c == '\n') {
      const bool separator = sepPossible && sepCount == SEPARATOR.size();
      sepCount = 0;
      sepPossible = true;
      sepCr = false;
      if (separator) {
        if (state == State::Keep) emit(data + runStart, i + 1 - runStart);
        state = State::Header;
        headerLen = 0;
        runStart = i + 1;
      }
      continue;
    }
    if (sepPossible) {
      if (sepCr) {
        sepPossible = false;
      } else if (c == '=') {
        if (++sepCount > SEPARATOR.size()) sepPossible = false;
      } else if (c == '\r') {
        sepCr = true;
      } else {
        sepPossible = false;
      }
    }
  }
  if (ok && state == State::Keep && runStart < len) emit(data + runStart, len - runStart);
  return ok;
}

bool ClippingsFilter::finish() {
  if (state == State::Header && headerLen > 0) endHeaderLine(false);
  return ok;
}

}  // namespace deckpoint::annotations
