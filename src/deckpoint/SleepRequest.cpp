#include "SleepRequest.h"

#include <atomic>

namespace deckpoint {

namespace {
std::atomic<bool> sleepRequested{false};
}  // namespace

void requestSleep() { sleepRequested.store(true, std::memory_order_release); }

bool takeSleepRequest() { return sleepRequested.exchange(false, std::memory_order_acq_rel); }

}  // namespace deckpoint
