#include "epaper_display.h"

#include <Arduino.h>
#include <EPD.h>
#include <EPD_Init.h>
#include <qrcode.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "epaper_core.h"
#include "epaper_logo.h"
#include "letters.h"
#include "notices.h"

namespace {

constexpr uint8_t kDisplayPowerPin = 7;
constexpr uint8_t kUpButtonPin = 6;
constexpr uint8_t kDownButtonPin = 4;
constexpr uint16_t kVisibleWidth = 792;
constexpr uint16_t kVisibleHeight = 272;
constexpr size_t kFramebufferBytes = (EPD_W / 8U) * EPD_H;
constexpr size_t kMessageColumns = 61;
constexpr size_t kMessageLines = 4;
constexpr uint8_t kQrVersion = 4;
constexpr uint8_t kQrScale = 6;
constexpr uint8_t kQrQuietZone = 4;
constexpr uint8_t kQrModuleCount = 4 * kQrVersion + 17;
constexpr size_t kQrBufferBytes =
    (static_cast<size_t>(kQrModuleCount) * kQrModuleCount + 7) / 8;

uint8_t framebuffer[kFramebufferBytes];
bool available = false;
bool initialized = false;
uint8_t fastRefreshes = 0;
size_t noticeIndex = 0;
bool showingWifiQr = false;
bool wifiQrForEmptyBoard = false;
uint16_t noticeSlotsShown = 0;
uint32_t lastRefreshAtMs = 0;
uint32_t displayedHash = 0;
EpaperCore::DebouncedButton upButton{};
EpaperCore::DebouncedButton downButton{};

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

bool buildWifiPayload(char* output, size_t outputSize) {
  if (!output || outputSize == 0) return false;
  size_t written = 0;
  const char prefix[] = "WIFI:T:nopass;S:";
  const char suffix[] = ";;";
  for (const char value : prefix) {
    if (!value) break;
    if (written + 1 >= outputSize) return false;
    output[written++] = value;
  }
  for (size_t index = 0; AppConfig::kApSsid[index]; ++index) {
    const char value = AppConfig::kApSsid[index];
    if (value == '\\' || value == ';' || value == ',' || value == ':' ||
        value == '"') {
      if (written + 1 >= outputSize) return false;
      output[written++] = '\\';
    }
    if (written + 1 >= outputSize) return false;
    output[written++] = value;
  }
  for (const char value : suffix) {
    if (!value) break;
    if (written + 1 >= outputSize) return false;
    output[written++] = value;
  }
  output[written] = '\0';
  return true;
}

void drawWifiQr() {
  char payload[128] = {};
  QRCode qr{};
  uint8_t qrData[kQrBufferBytes] = {};
  if (qrcode_getBufferSize(kQrVersion) > sizeof(qrData) ||
      !buildWifiPayload(payload, sizeof(payload)) ||
      qrcode_initText(&qr, qrData, kQrVersion, ECC_LOW, payload) != 0) {
    drawText(28, 110, "WI-FI QR UNAVAILABLE", 24);
    return;
  }

  const uint16_t qrOriginX = 13 + kQrQuietZone * kQrScale;
  const uint16_t qrOriginY = 13 + kQrQuietZone * kQrScale;
  for (uint8_t y = 0; y < qr.size; ++y) {
    for (uint8_t x = 0; x < qr.size; ++x) {
      if (!qrcode_getModule(&qr, x, y)) continue;
      const uint16_t left = qrOriginX + x * kQrScale;
      const uint16_t top = qrOriginY + y * kQrScale;
      EPD_DrawRectangle(left, top, left + kQrScale - 1,
                        top + kQrScale - 1, BLACK, 1);
    }
  }

  drawLogo(300, 30);
  drawText(350, 39, "STRAWBERRY POST", 24);
  drawText(300, 100, "JOIN THE WI-FI", 32);
  drawText(300, 148, "STRAWBERRY POST", 24);
  drawText(300, 180, "no internet", 24);
  drawText(300, 226, "THEN OPEN  post.local", 24);
}

void makeSnapshot(EpaperCore::Snapshot& snapshot) {
  snapshot = {};
  const size_t count = activeNoticeCount();
  if (showingWifiQr || count == 0) {
    snapshot.wifiQr = 1;
    return;
  } else {
    if (noticeIndex >= count) noticeIndex = 0;
    const NoticeRecord* notice = activeNoticeAt(noticeIndex);
    if (notice) {
      snapshot.noticeId = notice->id;
      snapshot.noticePosition = boundedCount(noticeIndex + 1);
      snapshot.noticeCount = boundedCount(count);
      EpaperCore::sanitizeAscii(noticeCategoryName(notice->category),
                               snapshot.category,
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
  if (snapshot.wifiQr) {
    drawWifiQr();
    return;
  }
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

bool isButtonPressed(uint8_t pin) { return digitalRead(pin) == LOW; }

bool showFrame(size_t nextIndex, uint32_t nowMs) {
  noticeIndex = nextIndex;
  // Manual navigation starts a fresh automatic-rotation interval, including
  // when there is only one notice and no framebuffer update is needed.
  lastRefreshAtMs = nowMs;
  if (refreshNow()) return true;
  available = false;
  Serial.println("E-paper refresh failed; display updates disabled.");
  return false;
}

}  // namespace

bool startEpaperDisplay() {
  pinMode(kUpButtonPin, INPUT_PULLUP);
  pinMode(kDownButtonPin, INPUT_PULLUP);
  const uint32_t startedAtMs = millis();
  EpaperCore::initializeButton(upButton, isButtonPressed(kUpButtonPin),
                              startedAtMs);
  EpaperCore::initializeButton(downButton, isButtonPressed(kDownButtonPin),
                              startedAtMs);
  pinMode(kDisplayPowerPin, OUTPUT);
  digitalWrite(kDisplayPowerPin, HIGH);
  Paint_NewImage(framebuffer, EPD_W, EPD_H, Rotation, WHITE);
  showingWifiQr = activeNoticeCount() == 0;
  wifiQrForEmptyBoard = showingWifiQr;
  noticeSlotsShown = showingWifiQr ? 0 : 1;
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
  if (!available) return;

  const uint32_t nowMs = millis();
  const bool upPressed = EpaperCore::buttonPressed(
      upButton, isButtonPressed(kUpButtonPin), nowMs,
      AppConfig::kButtonDebounceMs);
  const bool downPressed = EpaperCore::buttonPressed(
      downButton, isButtonPressed(kDownButtonPin), nowMs,
      AppConfig::kButtonDebounceMs);
  const size_t count = activeNoticeCount();

  if (count && upPressed) {
    showingWifiQr = false;
    wifiQrForEmptyBoard = false;
    noticeSlotsShown = 1;
    showFrame(EpaperCore::previousNoticeIndex(noticeIndex, count), nowMs);
    return;
  }
  if (count && downPressed) {
    showingWifiQr = false;
    wifiQrForEmptyBoard = false;
    noticeSlotsShown = 1;
    showFrame(EpaperCore::nextNoticeIndex(noticeIndex, count), nowMs);
    return;
  }
  if (nowMs - lastRefreshAtMs < AppConfig::kEpaperRotationIntervalMs) return;
  const EpaperCore::RotationState next =
      count != 0 && showingWifiQr && wifiQrForEmptyBoard
          ? EpaperCore::RotationState{0, 1, false}
          : EpaperCore::nextAutomaticFrame(
                {noticeIndex, noticeSlotsShown, showingWifiQr}, count,
                AppConfig::kEpaperNoticeSlotsPerWifiQr);
  showingWifiQr = next.wifiQr;
  wifiQrForEmptyBoard = showingWifiQr && count == 0;
  noticeSlotsShown = next.noticeSlotsShown;
  showFrame(next.noticeIndex, nowMs);
}

bool epaperDisplayAvailable() { return available; }
