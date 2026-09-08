#include "dns_server.h"

#include <Arduino.h>
#include <DNSServer.h>

#include "app_config.h"

namespace {

DNSServer dnsServer;
bool dnsRunning = false;

}  // namespace

bool startDnsServer() {
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.setTTL(AppConfig::kDnsTtlSeconds);
  dnsRunning = dnsServer.start(AppConfig::kDnsPort, "*", AppConfig::kApIp);

  if (dnsRunning) {
    Serial.print("Wildcard DNS started. Open ");
    Serial.println(AppConfig::kLocalUrl);
  } else {
    Serial.println("Failed to start wildcard DNS server.");
  }

  return dnsRunning;
}

void handleDnsRequests() {
  if (dnsRunning) {
    dnsServer.processNextRequest();
  }
}
