#include "device_time.h"

#include <Arduino.h>

namespace {

constexpr uint64_t kMinimumEpochSeconds = UINT64_C(1577836800);  // 2020-01-01
constexpr uint64_t kMaximumEpochSeconds = UINT64_C(4102444800);  // 2100-01-01

bool timeSet = false;
uint64_t epochAtSetSeconds = 0;
uint32_t uptimeAtSetMs = 0;

}  // namespace

bool setDeviceTime(uint64_t epochSeconds) {
  if (epochSeconds < kMinimumEpochSeconds ||
      epochSeconds >= kMaximumEpochSeconds) {
    return false;
  }
  uptimeAtSetMs = millis();
  epochAtSetSeconds = epochSeconds;
  timeSet = true;
  return true;
}

bool deviceTimeIsSet() { return timeSet; }

uint64_t deviceEpochSeconds() {
  if (!timeSet) return 0;
  return epochAtSetSeconds +
         static_cast<uint32_t>(millis() - uptimeAtSetMs) / 1000U;
}
