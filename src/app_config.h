#pragma once

#include <IPAddress.h>

namespace AppConfig {

constexpr char kApSsid[] = "STRAWBERRY POST no internet";
constexpr uint8_t kApChannel = 1;
constexpr uint8_t kMaxApClients = 8;

const IPAddress kApIp(192, 168, 4, 1);
const IPAddress kGateway(192, 168, 4, 1);
const IPAddress kSubnet(255, 255, 255, 0);

constexpr uint16_t kHttpPort = 80;

}  // namespace AppConfig
