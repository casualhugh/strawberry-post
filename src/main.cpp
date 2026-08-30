#include <Arduino.h>

#include "dns_server.h"
#include "letters.h"
#include "missed_connections.h"
#include "notices.h"
#include "storage.h"
#include "web_server.h"
#include "wifi_manager.h"

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println();
  Serial.println("Starting Strawberry Post...");

  if (!startAccessPoint()) {
    Serial.println("Access point startup failed; HTTP server not started.");
    return;
  }

  // HTTP remains usable at the AP address if DNS cannot be started.
  startStorage();
  startNotices();
  startMissedConnections();
  startLetters();
  startDnsServer();
  startWebServer();
  Serial.println("Strawberry Post is ready.");
}

void loop() {
  handleDnsRequests();
  handleWebRequests();
  delay(2);
}
