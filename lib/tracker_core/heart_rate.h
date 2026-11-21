// Adaptive-threshold heart rate detector. Pure C++ (no Arduino deps) so it can
// be unit-tested on the host.
#pragma once
#include <stdint.h>

class HeartRateMonitor {
 public:
  static constexpr uint8_t kIbiCount = 8;
  static constexpr uint8_t kMinBeats = 4;
  static constexpr uint32_t kWindowMs = 2000;
  static constexpr uint32_t kRefractoryMs = 300;
  static constexpr uint32_t kMaxIbiMs = 2000;
  static constexpr uint32_t kSignalLostMs = 3000;

  explicit HeartRateMonitor(int minAmplitude = 150) : minAmp_(minAmplitude) {}

  void feed(int sample, uint32_t now) {
    if (!windowStarted_) {
      windowStarted_ = true;
      windowStart_ = now;
      curMin_ = curMax_ = sample;
    }
    if (sample < curMin_) curMin_ = sample;
    if (sample > curMax_) curMax_ = sample;
    if (now - windowStart_ >= kWindowMs) {
      lo_ = curMin_;
      hi_ = curMax_;
      haveRange_ = true;
      windowStart_ = now;
      curMin_ = curMax_ = sample;
    }
    if (!haveRange_) return;

    const int range = hi_ - lo_;
    if (range < minAmp_) {
      above_ = false;
      return;
    }
    const int upper = lo_ + range * 6 / 10;
    const int lower = lo_ + range * 4 / 10;
    if (sample < lower) {
      above_ = false;
    } else if (sample > upper && !above_) {
      above_ = true;
      onBeat(now);
    }
  }

  // Returns 0 when there is no trustworthy reading.
  int bpm(uint32_t now) const {
    if (!hasBeat_ || count_ < kMinBeats || now - lastBeat_ > kSignalLostMs) return 0;
    uint32_t sum = 0;
    for (uint8_t i = 0; i < count_; i++) sum += ibi_[i];
    const uint32_t avg = sum / count_;
    return avg ? static_cast<int>(60000UL / avg) : 0;
  }

 private:
  void onBeat(uint32_t now) {
    if (hasBeat_) {
      const uint32_t ibi = now - lastBeat_;
      if (ibi < kRefractoryMs) return;  // ripple, not a beat
      if (ibi > kMaxIbiMs) {
        count_ = 0;  // gap: restart averaging
      } else {
        ibi_[head_] = ibi;
        head_ = (head_ + 1) % kIbiCount;
        if (count_ < kIbiCount) count_++;
      }
    }
    lastBeat_ = now;
    hasBeat_ = true;
  }

  int minAmp_;
  bool windowStarted_ = false, haveRange_ = false, above_ = false, hasBeat_ = false;
  uint32_t windowStart_ = 0, lastBeat_ = 0;
  int curMin_ = 0, curMax_ = 0, lo_ = 0, hi_ = 0;
  uint32_t ibi_[kIbiCount] = {};
  uint8_t head_ = 0, count_ = 0;
};

// Fires once when BPM stays beyond a limit for a sustained period: above it by
// default, or below it when `below` is true. A reading of 0 means "no signal"
// and never counts as a breach, so a lifted sensor cannot raise a low-BPM alert.
class BpmAlertGate {
 public:
  BpmAlertGate(int limit, uint32_t sustainMs, int hysteresis, uint32_t cooldownMs, bool below = false)
      : high_(limit), sustain_(sustainMs), hyst_(hysteresis), cooldown_(cooldownMs), below_(below) {}

  bool update(int bpm, uint32_t now) {
    if (bpm <= 0) {  // no signal: neither arm nor fire
      above_ = false;
      return false;
    }
    const bool breach = below_ ? bpm < high_ : bpm > high_;
    const bool recovered = below_ ? bpm >= high_ + hyst_ : bpm <= high_ - hyst_;
    if (breach) {
      if (!above_) {
        above_ = true;
        aboveSince_ = now;
      }
      const bool cooled = !fired_ || now - lastFire_ >= cooldown_;
      if (armed_ && cooled && now - aboveSince_ >= sustain_) {
        armed_ = false;
        fired_ = true;
        lastFire_ = now;
        return true;
      }
    } else if (recovered) {
      above_ = false;
      armed_ = true;
    }
    return false;
  }

 private:
  int high_;
  uint32_t sustain_;
  int hyst_;
  uint32_t cooldown_;
  bool below_;
  bool above_ = false, armed_ = true, fired_ = false;
  uint32_t aboveSince_ = 0, lastFire_ = 0;
};
