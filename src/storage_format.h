#pragma once

#include <stdint.h>

// These values identify Strawberry Post's own binary file formats. They are
// application-level sentinels, not values required by LittleFS. Keeping the
// four-character tag visible makes file-format checks easier to review.
constexpr uint32_t makeStorageMagic(char first, char second, char third,
                                    char fourth) {
  return (static_cast<uint32_t>(static_cast<uint8_t>(first)) << 24) |
         (static_cast<uint32_t>(static_cast<uint8_t>(second)) << 16) |
         (static_cast<uint32_t>(static_cast<uint8_t>(third)) << 8) |
         static_cast<uint32_t>(static_cast<uint8_t>(fourth));
}
