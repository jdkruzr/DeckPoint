#pragma once

// DECKPOINT: the EPUB reader's `:` commands (registry rows + handlers in
// ReaderCommands.cpp). The handlers are static members of a struct the reader
// befriends, so the constexpr table can name them while they reach the
// reader's private state.

#include <cstddef>

#include "deckpoint/Commands.h"
#include "deckpoint/KeyHelp.h"

namespace deckpoint::reader {

extern const CommandSpec READER_COMMANDS[];
extern const size_t READER_COMMAND_COUNT;
// "Commands" section of the reader's key help.
extern const KeyHelpExtra READER_COMMAND_HELP;

struct ReaderCommandHandlers;

}  // namespace deckpoint::reader
