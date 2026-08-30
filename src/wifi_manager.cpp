#include "wifi_manager.h"

#include <Arduino.h>
#include <WiFi.h>

#include "app_config.h"

bool startAccessPoint() {
  WiFi.mode(WIFI_AP);

  if (!WiFi.softAPConfig(AppConfig::kApIp, AppConfig::kGateway,
                         AppConfig::kSubnet)) {
    Serial.println("Failed to configure access point network.");
    return false;
  }

  const bool started =
      WiFi.softAP(AppConfig::kApSsid, nullptr, AppConfig::kApChannel, false,
                  AppConfig::kMaxApClients);
  if (!started) {
    Serial.println("Failed to start access point.");
    return false;
  }

  Serial.print("Access point SSID: ");
  Serial.println(AppConfig::kApSsid);
  Serial.print("Access point IP: ");
  Serial.println(WiFi.softAPIP());
  Serial.println("Open http://192.168.4.1/ in a browser.");
  return true;
}
