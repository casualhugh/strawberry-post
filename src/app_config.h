#pragma once

#include <IPAddress.h>

namespace AppConfig {

constexpr char kApSsid[] = "STRAWBERRY POST no internet";
constexpr char kLocalUrl[] = "http://post.local/";
constexpr uint8_t kApChannel = 1;
// Initial association limit only; it does not control radio range. Revisit this
// after real multi-phone load testing rather than treating eight as a target.
constexpr uint8_t kMaxApClients = 8;

const IPAddress kApIp(192, 168, 4, 1);
const IPAddress kGateway(192, 168, 4, 1);
const IPAddress kSubnet(255, 255, 255, 0);

constexpr uint16_t kHttpPort = 80;
constexpr uint16_t kDnsPort = 53;

constexpr uint32_t kPublicPostLifetimeMs = 8UL * 60UL * 60UL * 1000UL;
constexpr size_t kNoticeCategoryMaxBytes = 32;
constexpr size_t kNoticeMessageMaxBytes = 512;
constexpr size_t kMaxNotices = 32;
constexpr size_t kMissedTitleMaxBytes = 96;
constexpr size_t kMissedMessageMaxBytes = 512;
constexpr size_t kMaxMissedConnections = 32;

}  // namespace AppConfig
