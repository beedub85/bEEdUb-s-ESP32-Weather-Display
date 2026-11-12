#pragma once

#include <cstdint>

class ScreenIdle {
public:
  static constexpr uint32_t timeoutMs = 30'000;

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

  void update(uint32_t now) {
    if (!fingerDown && now - lastActivity >= timeoutMs) sleeping = true;
  }

  bool isSleeping() const { return sleeping; }

private:
  uint32_t lastActivity = 0;
  bool fingerDown = false;
  bool sleeping = false;
  bool consumeTouch = false;
};
