#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include "app_config.h"

enum class LetterStatus : uint8_t {
  Waiting = 0,
  Written,
  OutForDelivery,
  Delivered,
  CouldNotFind,
};

struct LetterRecord {
  uint32_t id;
  uint64_t createdAtEpochSeconds;
  uint32_t createdAtUptimeMs;
  uint32_t bootId;
  LetterStatus status;
  char trackingCode[AppConfig::kTrackingCodeMaxBytes + 1];
  char recipient[AppConfig::kLetterRecipientMaxBytes + 1];
  char location[AppConfig::kLetterLocationMaxBytes + 1];
  char message[AppConfig::kLetterMessageMaxBytes + 1];
  char sender[AppConfig::kLetterSenderMaxBytes + 1];
};

bool startLetters();
void registerLetterRoutes(WebServer& server);
const char* letterStatusName(LetterStatus status);
size_t letterCount();
uint32_t totalLettersSubmitted();
size_t lettersWithStatus(LetterStatus status);
const LetterRecord* letterAt(size_t index);
const LetterRecord* findLetterById(uint32_t id);
size_t loadLetterPage(uint32_t beforeId, LetterRecord* destination,
                      size_t capacity, bool& hasMore);
bool backfillLetterCreationTimes();
bool updateLetterStatus(uint32_t id, LetterStatus status);
bool deleteLetter(uint32_t id);
