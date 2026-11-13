#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <limits>

namespace localWeather {
inline time_t utcTimestamp(const char *s) {
  const size_t length = strlen(s);
  const char *format = length == 20 && s[19] == 'Z' ? "%Y-%m-%dT%H:%M:%SZ" :
      length == 14 && strspn(s, "0123456789") == 14 ? "%Y%m%d%H%M%S" : nullptr;
  if (!format) return 0;
  tm date{};
  const char *end = strptime(s, format, &date);
  if (!end || *end || date.tm_mon < 0 || date.tm_mon > 11 ||
      date.tm_mday < 1 || date.tm_sec > 59) return 0;

  const int year = date.tm_year + 1900;
  if (year < 1970) return 0;
  const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  constexpr int monthStarts[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365};
  const int daysInMonth = monthStarts[date.tm_mon + 1] - monthStarts[date.tm_mon] +
                          (leap && date.tm_mon == 1);
  if (date.tm_mday > daysInMonth) return 0;

  const int previous = year - 1;
  // Count whole Gregorian days since 1970 without consulting the display's time zone.
  const int64_t days = (year - 1970) * 365LL +
      (previous / 4 - previous / 100 + previous / 400) - (1969 / 4 - 1969 / 100 + 1969 / 400) +
      monthStarts[date.tm_mon] + (leap && date.tm_mon > 1) + date.tm_mday - 1;
  const int64_t seconds = days * 86400 + date.tm_hour * 3600 + date.tm_min * 60 + date.tm_sec;
  return seconds <= std::numeric_limits<time_t>::max() ? static_cast<time_t>(seconds) : 0;
}


inline bool postalPrefix(const char *input, char (&prefix)[4]) {
  char code[7]{};
  size_t count = 0;
  for (; *input; ++input) {
    char c = *input;
    if (c == ' ') continue;
    if (count == 6) return false;
    if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    if (count % 2 ? c < '0' || c > '9' : !c || !strchr("ABCEGHJKLMNPRSTVWXYZ", c)) return false;
    if (count == 0 && !strchr("ABCEGHJKLMNPRSTVXY", c)) return false;
    code[count++] = c;
  }
  if (count != 0 && count != 3 && count != 6) return false;
  memcpy(prefix, code, 3);
  prefix[3] = 0;
  return true;
}

enum Metric { Temperature, Pressure, Gust, Rain, MetricCount };
constexpr unsigned sampleCount = 24 * 12 + 1;
constexpr time_t historySeconds = 24 * 60 * 60;

struct Sample {
  time_t at = 0;
  float values[MetricCount]{};
};

struct History {
  std::array<Sample, sampleCount> samples{};
  unsigned next = 0, count = 0;
  void add(const Sample &sample) {
    if (count && sample.at <= at(count - 1).at) {
      if (sample.at == at(count - 1).at) return;
      next = count = 0;
    }
    samples[next] = sample;
    next = (next + 1) % sampleCount;
    if (count < sampleCount) ++count;
  }
  const Sample &at(unsigned i) const {
    return samples[(next + sampleCount - count + i) % sampleCount];
  }
};
}
