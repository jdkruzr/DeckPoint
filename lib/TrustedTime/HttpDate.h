#pragma once

#include <cstdint>

namespace trustedtime {

// Parses an HTTP Date header in RFC 7231 IMF-fixdate form
// ("Sun, 06 Nov 1994 08:49:37 GMT") into UTC epoch seconds. The weekday name
// must be valid but is not checked against the date; surrounding blanks are
// allowed. Obsolete RFC 850 / asctime forms are rejected. Pure (host-tested).
bool parseHttpDate(const char* header, int64_t& epochOut);

}  // namespace trustedtime
