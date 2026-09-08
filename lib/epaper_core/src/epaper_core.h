#pragma once

#include <stddef.h>
#include <stdint.h>

namespace EpaperCore {

constexpr size_t kDisplayTextBytes = 513;
constexpr size_t kDisplayCategoryBytes = 33;

struct Snapshot {
  uint8_t wifiQr;
  uint32_t noticeId;
  uint16_t noticePosition;
  uint16_t noticeCount;
  uint16_t waiting;
  uint16_t written;
  uint16_t outForDelivery;
  uint16_t delivered;
  char category[kDisplayCategoryBytes];
  char message[kDisplayTextBytes];
};

enum class RefreshKind : uint8_t { None, Fast, Full };

size_t sanitizeAscii(const char* input, char* output, size_t outputSize);
size_t wrapLine(const char* input, size_t start, size_t columns,
                char* output, size_t outputSize, size_t& next);
uint32_t snapshotHash(const Snapshot& snapshot);
RefreshKind chooseRefresh(bool initialized, uint32_t previousHash,
                          uint32_t nextHash, uint8_t fastRefreshes,
                          uint8_t fullRefreshInterval);
size_t previousNoticeIndex(size_t current, size_t count);
size_t nextNoticeIndex(size_t current, size_t count);

struct RotationState {
  size_t noticeIndex;
  uint16_t noticeSlotsShown;
  bool wifiQr;
};

RotationState nextAutomaticFrame(const RotationState& current,
                                 size_t noticeCount,
                                 uint8_t noticeSlotsPerWifiQr);

struct DebouncedButton {
  bool rawPressed;
  bool stablePressed;
  uint32_t rawChangedAtMs;
};

void initializeButton(DebouncedButton& button, bool pressed, uint32_t nowMs);
bool buttonPressed(DebouncedButton& button, bool pressed, uint32_t nowMs,
                   uint32_t debounceMs);

}  // namespace EpaperCore
