#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include "app_config.h"

struct NoticeRecord {
  uint32_t id;
  uint32_t createdAtMs;
  uint32_t expiresAtMs;
  uint32_t bootId;
  bool hidden;
  char category[AppConfig::kNoticeCategoryMaxBytes + 1];
  char message[AppConfig::kNoticeMessageMaxBytes + 1];
};

bool startNotices();
void registerNoticeRoutes(WebServer& server);
size_t activeNoticeCount();
uint32_t totalNoticesSubmitted();
size_t storedNoticeCount();
const NoticeRecord* noticeAt(size_t index);
bool setNoticeHidden(uint32_t id, bool hidden);
bool deleteNotice(uint32_t id);
