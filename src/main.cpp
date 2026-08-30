#include <Arduino.h>

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

  startWebServer();
  Serial.println("Strawberry Post is ready.");
}

void loop() {
  handleWebRequests();
  delay(2);
}
