#pragma once

// DECKPOINT: deferred "sleep now" (the `:sleep` command). Sleeping replaces the
// current activity and runs the activity loop, so it cannot start from inside
// an activity's key handler; main loop() picks the request up instead.

namespace deckpoint {

void requestSleep();
// True once per request; clears it.
bool takeSleepRequest();

}  // namespace deckpoint
