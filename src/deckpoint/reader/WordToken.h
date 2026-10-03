#pragma once

// DECKPOINT: which page tokens count as words for picking (pure, host-tested).

namespace deckpoint::reader {

// A token is selectable when it has an ASCII alphanumeric or a non-ASCII
// codepoint outside U+2000-U+206F (dashes, bullets and other General
// Punctuation that appear as standalone tokens are not words).
bool isSelectableToken(const char* text);

}  // namespace deckpoint::reader
