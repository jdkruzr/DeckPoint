#pragma once

// DECKPOINT: serial seeding of highlights for on-glass tests, handed from the
// main loop's CMD: parser to the open reader (EpubReaderActivity::annotationsTick):
//   CMD:ANNOTATE_TEST[:<note>]          words 3-8 of the current page
//   CMD:ANNOTATE:<pos0>||<pos1>[||<note>]  an explicit XPointer range
// A literal "\n" in <note> becomes a newline. Ignored outside the EPUB reader.

#include <string>

namespace deckpoint::annotations {

struct SeedRequest {
  bool testWords = false;  // ANNOTATE_TEST: pick words on the current page
  std::string pos0;
  std::string pos1;
  std::string note;
};

// Parses the text after "CMD:"; false when it is not an ANNOTATE command.
bool requestSeed(const char* command);
// True once per request; moves it into `out`.
bool takeSeedRequest(SeedRequest& out);

}  // namespace deckpoint::annotations
