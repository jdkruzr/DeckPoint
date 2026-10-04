#pragma once

// DECKPOINT: `:export` -- the open book's highlights and notes as
//   /DeckPoint/notes/<title>.md   (Obsidian-friendly, rewritten each time)
//   /DeckPoint/My Clippings.txt   (Kindle format; this book's old blocks are
//                                  replaced, other books' kept)
// Both go through a temp file and a replace. The clippings file is streamed
// through ClippingsFilter in 1 KB chunks, so its size does not matter for RAM.
//
// Locations are estimates: the spine item's byte offset in the book plus the
// highlight's visible-text offset when it is placed (chapter start when not),
// over 128 bytes per Kindle location; the percentage uses the same position.

#include <cstddef>
#include <memory>

class Epub;

namespace deckpoint::annotations {
class AnnotationStore;
}

namespace deckpoint::reader {

constexpr const char* EXPORT_DIR = "/DeckPoint";
constexpr const char* NOTES_EXPORT_DIR = "/DeckPoint/notes";
constexpr const char* CLIPPINGS_PATH = "/DeckPoint/My Clippings.txt";

// Writes both files; msg gets "Exported N: <md path>" or why not.
bool exportBookNotes(const std::shared_ptr<Epub>& epub, const annotations::AnnotationStore& store, char* msg,
                     size_t msgSize);

}  // namespace deckpoint::reader
