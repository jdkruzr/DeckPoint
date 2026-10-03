#pragma once

// DECKPOINT: `:` command registry. A screen owns a constexpr table of
// CommandSpec rows (handlers are plain function pointers taking the screen as
// ctx); everything here is pure (no Arduino, renderer or I18n lookups) so the
// host suite can drive parsing, matching and completion.

#include <I18nKeys.h>

#include <cstddef>
#include <cstdint>

namespace deckpoint {

// What the command line does after a handler ran.
enum class CommandResult : uint8_t {
  Restore,  // close; nothing changed under the line, put the page back
  Redraw,   // close; the handler requested a page re-render
  Left,     // close; the handler opened another screen or left the screen
  Message,  // stay open and show `message` (error or info); the typed text is kept
  Kept,     // the handler took the line over (it stays open, in the handler's hands)
};

// `args` is the trimmed rest of the line after the command name (never null,
// may be empty). On CommandResult::Message the handler fills `message`.
using CommandHandler = CommandResult (*)(void* ctx, const char* args, char* message, size_t messageSize);

struct CommandSpec {
  const char* name;     // lower case, matched case-insensitively (unique prefixes too)
  const char* alias;    // exact-match short form, or nullptr
  const char* argHint;  // shown in help ("N", "word"), or nullptr
  StrId help;
  CommandHandler handler;
};

enum class ParseKind : uint8_t {
  Empty,      // blank line
  Command,    // index + args
  Percent,    // ":42" (or ":42%" from a BLE keyboard)  value = 0..100
  Page,       // ":p42" / ":#42"  page of the current chapter, value = 1..MAX_PAGE
  Unknown,    // no name, alias or prefix matched
  Ambiguous,  // a prefix matched several names
  BadNumber,  // looks numeric but is not a percentage 0-100 or a page 1-MAX_PAGE
};

struct ParsedCommand {
  ParseKind kind = ParseKind::Empty;
  int index = -1;            // Command: row in the table
  uint16_t value = 0;        // Percent / Page
  char token[24] = {};       // first token, NUL-terminated (truncated when longer)
  char argBuf[64] = {};      // Command: trimmed arguments

  const char* name() const { return token; }
  const char* args() const { return argBuf; }
};

// Parses one line (without the leading ':').
ParsedCommand parseCommandLine(const char* line, const CommandSpec* table, size_t count);

constexpr uint16_t MAX_PAGE = 9999;

// "N" -> Percent (0-100). The built-in keyboard has no '%', so the sign is
// optional (accepted for BLE keyboards). "pN" / "#N" -> Page (1-MAX_PAGE).
// Anything else -> BadNumber (Empty for blank).
struct NumberTarget {
  ParseKind kind = ParseKind::BadNumber;
  uint16_t value = 0;
};
NumberTarget parseNumberTarget(const char* text);

// Tab / Alt+Space completion of the command name (first token only; nothing
// to do once a space was typed). Returns the number of matching names.
// One match: `out` = "name " (ready for arguments). Several: `out` = their
// longest common prefix, `candidates` = the names separated by two spaces
// (truncated to fit). `out` is left empty when it would not change the line.
size_t completeCommand(const char* line, const CommandSpec* table, size_t count, char* out, size_t outSize,
                       char* candidates, size_t candidatesSize);

// Loose name matching for arguments such as font families: case-insensitive,
// ignoring spaces, '-' and '_'.
enum class NameMatch : uint8_t { None, Substring, Prefix, Exact };
NameMatch matchName(const char* query, const char* candidate);

// Feed every candidate through add(); the best quality wins, ties make the
// pick ambiguous (count() > 1).
class NamePicker {
 public:
  explicit NamePicker(const char* query) : query(query) {}
  void add(int index, const char* candidate);
  NameMatch quality() const { return best; }
  int index() const { return bestIndex; }
  int count() const { return bestCount; }

 private:
  const char* query;
  NameMatch best = NameMatch::None;
  int bestIndex = -1;
  int bestCount = 0;
};

bool equalsNoCase(const char* a, const char* b);
// True when `text` starts with the first `prefixLen` chars of `prefix` (case-insensitive).
bool startsWithNoCase(const char* text, const char* prefix, size_t prefixLen);

}  // namespace deckpoint
