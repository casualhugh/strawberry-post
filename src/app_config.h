#pragma once

#include <IPAddress.h>

namespace AppConfig {

constexpr char kApSsid[] = "STRAWBERRY POST no internet";
constexpr char kLocalUrl[] = "http://post.local/";
constexpr char kAdminUsername[] = "postie";
// Change this in one place before deployment. Basic auth is appropriate only
// for this trusted, offline festival network and is not encrypted over HTTP.
constexpr char kAdminPassword[] = "change-me-postie";
constexpr uint8_t kApChannel = 1;
// Initial association limit only; it does not control radio range. Revisit this
// after real multi-phone load testing rather than treating eight as a target.
constexpr uint8_t kMaxApClients = 8;

const IPAddress kApIp(192, 168, 4, 1);
const IPAddress kGateway(192, 168, 4, 1);
const IPAddress kSubnet(255, 255, 255, 0);

constexpr uint16_t kHttpPort = 80;
constexpr uint16_t kDnsPort = 53;

constexpr uint32_t kPublicPostLifetimeMs = 48UL * 60UL * 60UL * 1000UL;
constexpr size_t kNoticeCategoryMaxBytes = 32;
constexpr size_t kNoticeMessageMaxBytes = 512;
constexpr size_t kMaxNotices = 32;
constexpr size_t kLetterRecipientMaxBytes = 160;
constexpr size_t kLetterLocationMaxBytes = 96;
constexpr size_t kLetterMessageMaxBytes = 768;
constexpr size_t kLetterSenderMaxBytes = 96;
constexpr size_t kTrackingCodeMaxBytes = 10;
constexpr size_t kMaxLetters = 32;
constexpr size_t kMaxFormBodyBytes = 4096;
constexpr size_t kMaxFormArguments = 8;
constexpr uint32_t kDuplicateWindowMs = 5000;
constexpr uint32_t kDiagnosticsIntervalMs = 30000;
constexpr bool kSerialRequestLogging = true;
// Briefly allow the serial device to enumerate before startup messages, then
// yield in the main loop so Wi-Fi/RTOS background work is not starved.
constexpr uint32_t kSerialStartupSettleMs = 250;
constexpr uint32_t kMainLoopYieldMs = 2;
// E-paper values are intentionally centralized for hardware tuning. The busy
// timeout prevents a disconnected or failed panel from blocking the website.
constexpr uint32_t kEpaperRotationIntervalMs = 30000;
constexpr uint8_t kEpaperFullRefreshInterval = 10;

}  // namespace AppConfig
