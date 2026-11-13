#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ctime>

namespace localWeather {
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
