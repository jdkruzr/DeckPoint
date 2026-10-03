#include "Commands.h"

#include <cstring>

namespace deckpoint {

namespace {

char lower(const char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }
bool isSpace(const char c) { return c == ' ' || c == '\t'; }
bool isDigit(const char c) { return c >= '0' && c <= '9'; }
bool isSeparator(const char c) { return c == ' ' || c == '-' || c == '_'; }

const char* skipSpaces(const char* p) {
  while (*p && isSpace(*p)) ++p;
  return p;
}

// Lower-cased copy without separators; returns its length.
size_t normalize(const char* in, char* out, const size_t outSize) {
  size_t n = 0;
  for (; *in && n + 1 < outSize; ++in) {
    if (isSeparator(*in)) continue;
    out[n++] = lower(*in);
  }
  out[n] = '\0';
  return n;
}

}  // namespace

bool equalsNoCase(const char* a, const char* b) {
  if (!a || !b) return false;
  while (*a && *b) {
    if (lower(*a++) != lower(*b++)) return false;
  }
  return *a == *b;
}

bool startsWithNoCase(const char* text, const char* prefix, const size_t prefixLen) {
  if (!text || !prefix) return false;
  for (size_t i = 0; i < prefixLen; ++i) {
    if (text[i] == '\0' || lower(text[i]) != lower(prefix[i])) return false;
  }
  return true;
}

NumberTarget parseNumberTarget(const char* text) {
  NumberTarget target;
  const char* p = skipSpaces(text ? text : "");
  if (*p == '\0') {
    target.kind = ParseKind::Empty;
    return target;
  }
  const bool page = *p == 'p' || *p == 'P' || *p == '#';
  if (page) p = skipSpaces(p + 1);
  uint32_t value = 0;
  int digits = 0;
  while (isDigit(*p)) {
    value = value * 10 + static_cast<uint32_t>(*p - '0');
    if (++digits > 4) return target;
    ++p;
  }
  if (digits == 0) return target;
  if (page) {
    if (*skipSpaces(p) != '\0' || value < 1 || value > MAX_PAGE) return target;
    target.kind = ParseKind::Page;
  } else {
    if (*p == '%') ++p;
    if (*skipSpaces(p) != '\0' || value > 100) return target;
    target.kind = ParseKind::Percent;
  }
  target.value = static_cast<uint16_t>(value);
  return target;
}

ParsedCommand parseCommandLine(const char* line, const CommandSpec* table, const size_t count) {
  ParsedCommand parsed;
  const char* p = skipSpaces(line ? line : "");
  if (*p == '\0') return parsed;

  // "42", "p42", "# 42": numeric targets (no command name starts with p + digit).
  const char* afterPagePrefix = (*p == 'p' || *p == 'P' || *p == '#') ? skipSpaces(p + 1) : p;
  if (isDigit(*p) || *p == '#' || (afterPagePrefix != p && isDigit(*afterPagePrefix))) {
    const NumberTarget number = parseNumberTarget(p);
    parsed.kind = number.kind;
    parsed.value = number.value;
    return parsed;
  }

  const char* tokenEnd = p;
  while (*tokenEnd && !isSpace(*tokenEnd)) ++tokenEnd;
  const size_t tokenLen = static_cast<size_t>(tokenEnd - p);
  const size_t copyLen = tokenLen < sizeof(parsed.token) - 1 ? tokenLen : sizeof(parsed.token) - 1;
  memcpy(parsed.token, p, copyLen);
  parsed.token[copyLen] = '\0';

  const char* args = skipSpaces(tokenEnd);
  size_t argLen = strlen(args);
  while (argLen > 0 && isSpace(args[argLen - 1])) --argLen;
  if (argLen > sizeof(parsed.argBuf) - 1) argLen = sizeof(parsed.argBuf) - 1;
  memcpy(parsed.argBuf, args, argLen);
  parsed.argBuf[argLen] = '\0';

  int match = -1;
  int prefixMatches = 0;
  if (tokenLen < sizeof(parsed.token)) {
    for (size_t i = 0; i < count && match < 0; ++i) {
      if (equalsNoCase(table[i].name, parsed.token) || equalsNoCase(table[i].alias, parsed.token)) {
        match = static_cast<int>(i);
      }
    }
    if (match < 0) {
      for (size_t i = 0; i < count; ++i) {
        if (startsWithNoCase(table[i].name, parsed.token, tokenLen)) {
          if (prefixMatches++ == 0) match = static_cast<int>(i);
        }
      }
      if (prefixMatches > 1) {
        parsed.kind = ParseKind::Ambiguous;
        return parsed;
      }
    }
  }
  if (match < 0) {
    parsed.kind = ParseKind::Unknown;
    return parsed;
  }
  parsed.kind = ParseKind::Command;
  parsed.index = match;
  return parsed;
}

size_t completeCommand(const char* line, const CommandSpec* table, const size_t count, char* out,
                       const size_t outSize, char* candidates, const size_t candidatesSize) {
  if (outSize > 0) out[0] = '\0';
  if (candidatesSize > 0) candidates[0] = '\0';
  const char* p = skipSpaces(line ? line : "");
  for (const char* q = p; *q; ++q) {
    if (isSpace(*q)) return 0;  // arguments are not completed
  }
  const size_t typed = strlen(p);

  size_t matches = 0;
  size_t common = 0;  // shared prefix length of the matching names
  const char* first = nullptr;
  size_t candLen = 0;
  bool candidatesFull = false;
  for (size_t i = 0; i < count; ++i) {
    const char* name = table[i].name;
    if (!startsWithNoCase(name, p, typed)) continue;
    if (matches++ == 0) {
      first = name;
      common = strlen(name);
    } else {
      size_t k = 0;
      while (k < common && name[k] && lower(name[k]) == lower(first[k])) ++k;
      common = k;
    }
    if (candidatesSize > 0 && !candidatesFull) {
      const size_t nameLen = strlen(name);
      const size_t sep = candLen > 0 ? 2 : 0;
      candidatesFull = candLen + sep + nameLen + 1 > candidatesSize;  // list stops at the first misfit
      if (!candidatesFull) {
        if (sep) {
          candidates[candLen++] = ' ';
          candidates[candLen++] = ' ';
        }
        memcpy(candidates + candLen, name, nameLen);
        candLen += nameLen;
        candidates[candLen] = '\0';
      }
    }
  }
  if (matches == 0 || outSize == 0) return matches;

  if (matches == 1) {
    const size_t len = strlen(first);
    if (len + 2 > outSize) return matches;
    memcpy(out, first, len);
    out[len] = ' ';
    out[len + 1] = '\0';
  } else if (common > typed && common + 1 <= outSize) {
    memcpy(out, first, common);
    out[common] = '\0';
  }
  return matches;
}

NameMatch matchName(const char* query, const char* candidate) {
  char q[48];
  char c[48];
  const size_t qLen = normalize(query ? query : "", q, sizeof(q));
  const size_t cLen = normalize(candidate ? candidate : "", c, sizeof(c));
  if (qLen == 0 || cLen == 0) return NameMatch::None;
  if (qLen == cLen && memcmp(q, c, qLen) == 0) return NameMatch::Exact;
  if (qLen < cLen && memcmp(q, c, qLen) == 0) return NameMatch::Prefix;
  if (strstr(c, q) != nullptr) return NameMatch::Substring;
  return NameMatch::None;
}

void NamePicker::add(const int candidateIndex, const char* candidate) {
  const NameMatch m = matchName(query, candidate);
  if (m == NameMatch::None) return;
  if (m > best) {
    best = m;
    bestIndex = candidateIndex;
    bestCount = 1;
  } else if (m == best) {
    ++bestCount;
  }
}

}  // namespace deckpoint
