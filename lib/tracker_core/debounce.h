#pragma once
#include <stdint.h>

// Debounces a boolean input. update() returns true once per press (rising edge
// of the logical "pressed" state).
class Debouncer {
 public:
  explicit Debouncer(uint32_t delayMs = 50) : delay_(delayMs) {}

  bool update(bool pressedRaw, uint32_t now) {
    if (pressedRaw != lastRaw_) lastChange_ = now;
    lastRaw_ = pressedRaw;
    if (now - lastChange_ > delay_ && pressedRaw != stable_) {
      stable_ = pressedRaw;
      return stable_;
    }
    return false;
  }

 private:
  uint32_t delay_;
  uint32_t lastChange_ = 0;
  bool lastRaw_ = false, stable_ = false;
};
