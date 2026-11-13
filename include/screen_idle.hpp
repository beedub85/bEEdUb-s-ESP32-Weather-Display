#pragma once

#include <cstdint>

class ScreenIdle {
public:
  static constexpr uint32_t defaultTimeoutSeconds = 60;
  static constexpr uint32_t timeoutOptions[] = {15, 30, 60, 120, 300};

  static constexpr uint32_t normalizeTimeout(uint32_t seconds) {
    for (uint32_t option : timeoutOptions) {
      if (seconds == option) return seconds;
    }
    return defaultTimeoutSeconds;
  }

  void begin(uint32_t now) { lastActivity = now; }

  // A wake touch is consumed until release, so it cannot click or swipe.
  bool acceptsTouch(bool pressed, uint32_t now) {
    if (!pressed) {
      fingerDown = false;
      consumeTouch = false;
      return false;
    }

    fingerDown = true;
    lastActivity = now;
    if (sleeping) {
      sleeping = false;
      consumeTouch = true;
    }
    return !consumeTouch;
  }

  void update(uint32_t now, uint32_t timeoutSeconds) {
    if (!fingerDown && now - lastActivity >= timeoutSeconds * 1000) sleeping = true;
  }

  bool isSleeping() const { return sleeping; }

private:
  uint32_t lastActivity = 0;
  bool fingerDown = false;
  bool sleeping = false;
  bool consumeTouch = false;
};
