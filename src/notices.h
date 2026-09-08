#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include "app_config.h"

enum class NoticeCategory : uint8_t {
  General = 0,
  MissedConnection,
  LostAndFound,
  EventSchedule,
  RideShare,
  HelpWanted,
  ForSaleOrSwap,
};

struct NoticeRecord {
  uint32_t id;
  uint64_t createdAtEpochSeconds;
  uint32_t createdAtUptimeMs;
  uint32_t bootId;
  uint8_t hidden;
  NoticeCategory category;
  char message[AppConfig::kNoticeMessageMaxBytes + 1];
};

bool startNotices();
void registerNoticeRoutes(WebServer& server);
const char* noticeCategoryName(NoticeCategory category);
bool parseNoticeCategory(const char* name, NoticeCategory& category);
size_t activeNoticeCount();
size_t publicNoticeCount();
uint32_t totalNoticesSubmitted();
size_t storedNoticeCount();
const NoticeRecord* noticeAt(size_t index);
const NoticeRecord* activeNoticeAt(size_t index);
size_t loadNoticePage(uint32_t beforeId, NoticeRecord* destination,
                      size_t capacity, bool publicOnly, bool& hasMore);
bool backfillNoticeCreationTimes();
bool setNoticeHidden(uint32_t id, bool hidden);
bool deleteNotice(uint32_t id);
