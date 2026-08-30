#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include "app_config.h"

struct MissedConnectionRecord {
  uint32_t id;
  uint32_t createdAtMs;
  uint32_t expiresAtMs;
  uint32_t bootId;
  bool hidden;
  char title[AppConfig::kMissedTitleMaxBytes + 1];
  char message[AppConfig::kMissedMessageMaxBytes + 1];
};

bool startMissedConnections();
void registerMissedConnectionRoutes(WebServer& server);
size_t activeMissedConnectionCount();
uint32_t totalMissedConnectionsSubmitted();
