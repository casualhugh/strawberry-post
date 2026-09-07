#include "epaper_display.h"

#include <Arduino.h>
#include <EPD.h>
#include <EPD_Init.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "epaper_core.h"
#include "epaper_logo.h"
#include "letters.h"
#include "notices.h"

namespace {

constexpr uint8_t kDisplayPowerPin = 7;
constexpr uint16_t kVisibleWidth = 792;
constexpr uint16_t kVisibleHeight = 272;
constexpr size_t kFramebufferBytes = (EPD_W / 8U) * EPD_H;
constexpr size_t kMessageColumns = 61;
constexpr size_t kMessageLines = 4;

uint8_t framebuffer[kFramebufferBytes];
bool available = false;
bool initialized = false;
uint8_t fastRefreshes = 0;
size_t noticeIndex = 0;
uint32_t lastRefreshAtMs = 0;
uint32_t displayedHash = 0;

uint16_t boundedCount(size_t count) {
  return count > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(count);
}

void drawLogo(uint16_t x, uint16_t y) {
  constexpr uint16_t width = 34;
  constexpr uint16_t height = 43;
  constexpr uint16_t bytesPerRow = 5;
  for (uint16_t row = 0; row < height; ++row) {
    for (uint16_t column = 0; column < width; ++column) {
      const uint8_t value = kEpaperLogo34x43[row * bytesPerRow + column / 8];
      if (value & (0x80U >> (column % 8))) {
        Paint_SetPixel(x + column, y + row, BLACK);
      }
    }
  }
}

void drawText(uint16_t x, uint16_t y, const char* text, uint16_t size,
              uint16_t colour = BLACK) {
  EPD_ShowString(x, y, text, size, colour);
}

void makeSnapshot(EpaperCore::Snapshot& snapshot) {
  snapshot = {};
  const size_t count = activeNoticeCount();
  if (count == 0) {
    noticeIndex = 0;
    strlcpy(snapshot.category, "NOTICE BOARD", sizeof(snapshot.category));
    strlcpy(snapshot.message,
            "Nothing pinned yet. Either everyone's behaving or nobody's "
            "awake.",
            sizeof(snapshot.message));
  } else {
    if (noticeIndex >= count) noticeIndex = 0;
    const NoticeRecord* notice = activeNoticeAt(noticeIndex);
    if (notice) {
      snapshot.noticeId = notice->id;
      snapshot.noticePosition = boundedCount(noticeIndex + 1);
      snapshot.noticeCount = boundedCount(count);
      EpaperCore::sanitizeAscii(notice->category, snapshot.category,
                               sizeof(snapshot.category));
      EpaperCore::sanitizeAscii(notice->message, snapshot.message,
                               sizeof(snapshot.message));
    }
  }
  snapshot.waiting = boundedCount(lettersWithStatus(LetterStatus::Waiting));
  snapshot.written = boundedCount(lettersWithStatus(LetterStatus::Written));
  snapshot.outForDelivery =
      boundedCount(lettersWithStatus(LetterStatus::OutForDelivery));
  snapshot.delivered = boundedCount(lettersWithStatus(LetterStatus::Delivered));
}

void drawSnapshot(const EpaperCore::Snapshot& snapshot) {
  Paint_Clear(WHITE);
  EPD_DrawRectangle(0, 0, kVisibleWidth - 1, kVisibleHeight - 1, BLACK, 0);
  EPD_DrawLine(0, 50, kVisibleWidth - 1, 50, BLACK);
  EPD_DrawLine(0, 219, kVisibleWidth - 1, 219, BLACK);
  drawLogo(12, 4);
  drawText(60, 13, "STRAWBERRY POST", 24);

  char category[24] = {};
  strlcpy(category, snapshot.category, sizeof(category));
  EPD_DrawRectangle(388, 9, 575, 40, BLACK, 1);
  drawText(400, 17, category, 16, WHITE);

  char position[18] = {};
  if (snapshot.noticeCount) {
    snprintf(position, sizeof(position), "NOTICE %u/%u",
             snapshot.noticePosition, snapshot.noticeCount);
  } else {
    strlcpy(position, "NO NOTICES", sizeof(position));
  }
  drawText(650, 17, position, 16);

  size_t next = 0;
  for (size_t lineIndex = 0;
       lineIndex < kMessageLines && snapshot.message[next]; ++lineIndex) {
    char line[kMessageColumns + 1] = {};
    EpaperCore::wrapLine(snapshot.message, next, kMessageColumns, line,
                         sizeof(line), next);
    drawText(28, 72 + lineIndex * 34, line, 24);
  }

  struct FooterItem {
    uint16_t x;
    const char* label;
    uint16_t value;
  };
  const FooterItem footer[] = {
      {18, "WAITING", snapshot.waiting},
      {210, "WRITTEN", snapshot.written},
      {395, "OUT WITH POSTIE", snapshot.outForDelivery},
      {622, "DELIVERED", snapshot.delivered},
  };
  for (const FooterItem& item : footer) {
    char value[6] = {};
    snprintf(value, sizeof(value), "%u", item.value);
    drawText(item.x, 229, value, 24);
    drawText(item.x + 40, 235, item.label, 16);
  }
}

bool updatePanel(EpaperCore::RefreshKind refresh) {
  EPD_ResetOperationStatus();
  EPD_GPIOInit();
  EPD_FastMode1Init();
  if (refresh == EpaperCore::RefreshKind::Full) {
    EPD_Display_Clear();
    EPD_Update();
    if (EPD_LastOperationSucceeded()) {
      EPD_GPIOInit();
      EPD_FastMode1Init();
    }
  }
  if (EPD_LastOperationSucceeded()) {
    EPD_Display(framebuffer);
    EPD_FastUpdate();
  }
  EPD_DeepSleep();
  return EPD_LastOperationSucceeded();
}

bool refreshNow() {
  EpaperCore::Snapshot snapshot{};
  makeSnapshot(snapshot);
  const uint32_t nextHash = EpaperCore::snapshotHash(snapshot);
  const EpaperCore::RefreshKind refresh = EpaperCore::chooseRefresh(
      initialized, displayedHash, nextHash, fastRefreshes,
      AppConfig::kEpaperFullRefreshInterval);
  if (refresh == EpaperCore::RefreshKind::None) return true;

  drawSnapshot(snapshot);
  if (!updatePanel(refresh)) return false;
  displayedHash = nextHash;
  initialized = true;
  fastRefreshes = refresh == EpaperCore::RefreshKind::Full
                      ? 0
                      : static_cast<uint8_t>(fastRefreshes + 1);
  return true;
}

}  // namespace

bool startEpaperDisplay() {
  pinMode(kDisplayPowerPin, OUTPUT);
  digitalWrite(kDisplayPowerPin, HIGH);
  Paint_NewImage(framebuffer, EPD_W, EPD_H, Rotation, WHITE);
  available = refreshNow();
  lastRefreshAtMs = millis();
  if (!available) {
    Serial.println("E-paper unavailable; continuing without the display.");
  } else {
    Serial.println("E-paper display ready.");
  }
  return available;
}

void handleEpaperDisplay() {
  if (!available ||
      millis() - lastRefreshAtMs < AppConfig::kEpaperRotationIntervalMs) {
    return;
  }
  lastRefreshAtMs = millis();
  noticeIndex = EpaperCore::nextNoticeIndex(noticeIndex, activeNoticeCount());
  if (!refreshNow()) {
    available = false;
    Serial.println("E-paper refresh failed; display updates disabled.");
  }
}

bool epaperDisplayAvailable() { return available; }
