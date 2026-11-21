// Alert model, priority queue and SMS formatting. Pure C++ for host testing.
#pragma once
#include <stdint.h>
#include <stdio.h>

// The numeric value is the queue priority: higher is sent first.
enum class AlertReason : uint8_t { GpsFix = 0, HighBpm = 1, LowBpm = 2, Sos = 3 };

struct Alert {
  AlertReason reason = AlertReason::GpsFix;
  bool hasLocation = false;
  double lat = 0, lng = 0;
  uint32_t locAgeMs = 0;
  int bpm = 0;  // 0 = unknown
  uint8_t attempts = 0;
};

inline uint8_t alertPriority(AlertReason r) { return static_cast<uint8_t>(r); }

inline const char* alertReasonText(AlertReason r) {
  switch (r) {
    case AlertReason::Sos: return "SOS - manual button press";
    case AlertReason::HighBpm: return "High heart rate";
    case AlertReason::LowBpm: return "Low heart rate";
    default: return "GPS fix acquired";
  }
}

// Whether an alert should also trigger an automatic phone call.
inline bool wantsCall(AlertReason r, bool callOnSos, bool callOnBpm) {
  if (r == AlertReason::Sos) return callOnSos;
  if (r == AlertReason::HighBpm || r == AlertReason::LowBpm) return callOnBpm;
  return false;
}

// Writes a single-part (<=160 char) SMS. Returns length written.
inline int formatAlert(char* out, size_t n, const Alert& a, uint32_t staleMs = 30000) {
  int len = snprintf(out, n, "ALERT: %s\n", alertReasonText(a.reason));
  if (a.bpm > 0) len += snprintf(out + len, n - len, "BPM: %d\n", a.bpm);
  else len += snprintf(out + len, n - len, "BPM: --\n");
  if (a.hasLocation) {
    len += snprintf(out + len, n - len, "%shttps://maps.google.com/?q=%.6f,%.6f",
                    a.locAgeMs > staleMs ? "Last known: " : "", a.lat, a.lng);
  } else {
    len += snprintf(out + len, n - len, "Location unknown (no GPS fix)");
  }
  return len;
}

// Small fixed-size queue. Pops highest priority first (FIFO within a priority).
// When full, evicts the lowest-priority oldest entry unless the incoming alert
// is lower priority than everything queued.
template <uint8_t N = 4>
class AlertQueue {
 public:
  bool push(const Alert& a) {
    if (count_ == N) {
      uint8_t victim = 0;
      for (uint8_t i = 1; i < count_; i++)
        if (alertPriority(items_[i].reason) < alertPriority(items_[victim].reason)) victim = i;
      if (alertPriority(items_[victim].reason) > alertPriority(a.reason)) return false;
      removeAt(victim);
    }
    items_[count_++] = a;
    return true;
  }

  bool pop(Alert& out) {
    if (count_ == 0) return false;
    uint8_t best = 0;
    for (uint8_t i = 1; i < count_; i++)
      if (alertPriority(items_[i].reason) > alertPriority(items_[best].reason)) best = i;
    out = items_[best];
    removeAt(best);
    return true;
  }

  uint8_t size() const { return count_; }

 private:
  void removeAt(uint8_t i) {
    for (; i + 1 < count_; i++) items_[i] = items_[i + 1];
    count_--;
  }
  Alert items_[N];
  uint8_t count_ = 0;
};
