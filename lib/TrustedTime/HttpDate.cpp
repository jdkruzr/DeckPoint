#include "HttpDate.h"

#include <cstring>

namespace trustedtime {

namespace {

constexpr const char* DAYS[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
constexpr const char* MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

bool isDigit(const char c) { return c >= '0' && c <= '9'; }

// Exactly `count` digits at s, advanced past them.
bool digits(const char*& s, const int count, int& out) {
  out = 0;
  for (int i = 0; i < count; i++) {
    if (!isDigit(s[i])) return false;
    out = out * 10 + (s[i] - '0');
  }
  s += count;
  return true;
}

bool literal(const char*& s, const char* text) {
  const size_t n = strlen(text);
  if (strncmp(s, text, n) != 0) return false;
  s += n;
  return true;
}

// Index of the 3-letter name at s in names[count], advancing past it; -1 if none.
int name3(const char*& s, const char* const* names, const int count) {
  for (int i = 0; i < count; i++) {
    if (strncmp(s, names[i], 3) == 0) {
      s += 3;
      return i;
    }
  }
  return -1;
}

bool leapYear(const int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

int daysInMonth(const int y, const int m) {
  constexpr int DAYS_IN[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return m == 2 && leapYear(y) ? 29 : DAYS_IN[m - 1];
}

// Days since 1970-01-01 for a civil date (Hinnant's days_from_civil).
int64_t daysFromCivil(int y, const unsigned m, const unsigned d) {
  y -= m <= 2 ? 1 : 0;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const auto yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

}  // namespace

bool parseHttpDate(const char* header, int64_t& epochOut) {
  if (!header) return false;
  const char* s = header;
  while (*s == ' ' || *s == '\t') s++;
  if (name3(s, DAYS, 7) < 0 || !literal(s, ", ")) return false;
  int day, year, hour, minute, second;
  if (!digits(s, 2, day) || !literal(s, " ")) return false;
  const int month = name3(s, MONTHS, 12) + 1;
  if (month <= 0 || !literal(s, " ")) return false;
  if (!digits(s, 4, year) || !literal(s, " ")) return false;
  if (!digits(s, 2, hour) || !literal(s, ":") || !digits(s, 2, minute) || !literal(s, ":") || !digits(s, 2, second)) {
    return false;
  }
  if (!literal(s, " GMT")) return false;
  while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
  if (*s != '\0') return false;
  // 60 is a leap second; the clock cannot hold it, so it reads as :59.
  if (year < 1970 || year > 2099 || day < 1 || day > daysInMonth(year, month) || hour > 23 || minute > 59 ||
      second > 60) {
    return false;
  }
  if (second == 60) second = 59;
  epochOut = daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day)) * 86400 + hour * 3600 +
             minute * 60 + second;
  return true;
}

}  // namespace trustedtime
