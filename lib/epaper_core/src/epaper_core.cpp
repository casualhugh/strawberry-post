#include "epaper_core.h"

#include <string.h>

namespace EpaperCore {
namespace {

uint32_t appendHash(uint32_t hash, const void* value, size_t size) {
  const uint8_t* bytes = static_cast<const uint8_t*>(value);
  for (size_t index = 0; index < size; ++index) {
    hash ^= bytes[index];
    hash *= UINT32_C(16777619);
  }
  return hash;
}

}  // namespace

size_t sanitizeAscii(const char* input, char* output, size_t outputSize) {
  if (outputSize == 0) return 0;
  size_t written = 0;
  bool replacingUtf8 = false;
  for (size_t index = 0; input && input[index] && written + 1 < outputSize;
       ++index) {
    const uint8_t value = static_cast<uint8_t>(input[index]);
    if (value >= 32 && value <= 126) {
      output[written++] = static_cast<char>(value);
      replacingUtf8 = false;
    } else if (value == '\n' || value == '\r' || value == '\t') {
      if (written && output[written - 1] != ' ') output[written++] = ' ';
      replacingUtf8 = false;
    } else if ((value & 0xc0) != 0x80) {
      output[written++] = '?';
      replacingUtf8 = true;
    } else if (!replacingUtf8) {
      output[written++] = '?';
      replacingUtf8 = true;
    }
  }
  while (written && output[written - 1] == ' ') --written;
  output[written] = '\0';
  return written;
}

size_t wrapLine(const char* input, size_t start, size_t columns,
                char* output, size_t outputSize, size_t& next) {
  if (!input || outputSize == 0 || columns == 0) {
    next = start;
    return 0;
  }
  const size_t length = strlen(input);
  while (start < length && input[start] == ' ') ++start;
  size_t end = start;
  size_t lastSpace = static_cast<size_t>(-1);
  while (end < length && end - start < columns) {
    if (input[end] == ' ') lastSpace = end;
    ++end;
  }
  if (end < length && lastSpace != static_cast<size_t>(-1)) end = lastSpace;
  size_t count = end - start;
  if (count >= outputSize) count = outputSize - 1;
  memcpy(output, input + start, count);
  output[count] = '\0';
  next = end;
  while (next < length && input[next] == ' ') ++next;
  return count;
}

uint32_t snapshotHash(const Snapshot& snapshot) {
  uint32_t hash = UINT32_C(2166136261);
  hash = appendHash(hash, &snapshot.wifiQr, sizeof(snapshot.wifiQr));
  hash = appendHash(hash, &snapshot.noticeId, sizeof(snapshot.noticeId));
  hash = appendHash(hash, &snapshot.noticePosition, sizeof(snapshot.noticePosition));
  hash = appendHash(hash, &snapshot.noticeCount, sizeof(snapshot.noticeCount));
  hash = appendHash(hash, &snapshot.waiting, sizeof(snapshot.waiting));
  hash = appendHash(hash, &snapshot.written, sizeof(snapshot.written));
  hash = appendHash(hash, &snapshot.outForDelivery,
                    sizeof(snapshot.outForDelivery));
  hash = appendHash(hash, &snapshot.delivered, sizeof(snapshot.delivered));
  hash = appendHash(hash, snapshot.category, strlen(snapshot.category));
  return appendHash(hash, snapshot.message, strlen(snapshot.message));
}

RefreshKind chooseRefresh(bool initialized, uint32_t previousHash,
                          uint32_t nextHash, uint8_t fastRefreshes,
                          uint8_t fullRefreshInterval) {
  if (!initialized) return RefreshKind::Full;
  if (previousHash == nextHash) return RefreshKind::None;
  if (fullRefreshInterval == 0 || fastRefreshes >= fullRefreshInterval)
    return RefreshKind::Full;
  return RefreshKind::Fast;
}

size_t previousNoticeIndex(size_t current, size_t count) {
  if (count == 0) return 0;
  current %= count;
  return current == 0 ? count - 1 : current - 1;
}

size_t nextNoticeIndex(size_t current, size_t count) {
  return count == 0 ? 0 : (current + 1) % count;
}

RotationState nextAutomaticFrame(const RotationState& current,
                                 size_t noticeCount,
                                 uint8_t noticeSlotsPerWifiQr) {
  if (noticeCount == 0) return RotationState{0, 0, true};
  if (current.wifiQr) {
    return RotationState{nextNoticeIndex(current.noticeIndex, noticeCount), 1,
                         false};
  }
  if (noticeSlotsPerWifiQr != 0 &&
      current.noticeSlotsShown >= noticeSlotsPerWifiQr) {
    return RotationState{current.noticeIndex % noticeCount, 0, true};
  }
  const uint16_t slots = current.noticeSlotsShown == UINT16_MAX
                             ? UINT16_MAX
                             : current.noticeSlotsShown + 1;
  return RotationState{nextNoticeIndex(current.noticeIndex, noticeCount), slots,
                       false};
}

void initializeButton(DebouncedButton& button, bool pressed, uint32_t nowMs) {
  button.rawPressed = pressed;
  button.stablePressed = pressed;
  button.rawChangedAtMs = nowMs;
}

bool buttonPressed(DebouncedButton& button, bool pressed, uint32_t nowMs,
                   uint32_t debounceMs) {
  if (pressed != button.rawPressed) {
    button.rawPressed = pressed;
    button.rawChangedAtMs = nowMs;
  }
  if (button.rawPressed == button.stablePressed ||
      nowMs - button.rawChangedAtMs < debounceMs) {
    return false;
  }
  button.stablePressed = button.rawPressed;
  return button.stablePressed;
}

}  // namespace EpaperCore
