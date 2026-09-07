#include "public_ui.h"

#include "diagnostics.h"
#include "generated_web_assets.h"
#include "letters.h"
#include "notices.h"

namespace {

void sendStats(WebServer& server) {
  recordHttpRequest(server);
  String response = F("{\"lettersSubmitted\":");
  response += totalLettersSubmitted();
  response += F(",\"lettersWaiting\":");
  response += lettersWithStatus(LetterStatus::Waiting);
  response += F(",\"lettersOutForDelivery\":");
  response += lettersWithStatus(LetterStatus::OutForDelivery);
  response += F(",\"lettersDelivered\":");
  response += lettersWithStatus(LetterStatus::Delivered);
  response += F(",\"noticesActive\":");
  response += activeNoticeCount();
  response += F(",\"noticesSubmitted\":");
  response += totalNoticesSubmitted();
  response += '}';
  server.send(200, "application/json; charset=utf-8", response);
}

}  // namespace

void sendPublicHome(WebServer& server) {
  recordHttpRequest(server);
  // Canonical sources live in web/, but the build embeds their generated copy
  // in program flash. Serving PROGMEM avoids a runtime filesystem read and
  // keeps the UI available even when LittleFS data storage is unavailable.
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html; charset=utf-8", WebAssets::kHomePage);
}

void registerPublicUiRoutes(WebServer& server) {
  server.on("/style.css", HTTP_GET, [&server]() {
    recordHttpRequest(server);
    server.sendHeader("Cache-Control", "public, max-age=3600");
    server.send_P(200, "text/css; charset=utf-8", WebAssets::kPublicStyles);
  });
  server.on("/logo.svg", HTTP_GET, [&server]() {
    recordHttpRequest(server);
    server.sendHeader("Cache-Control", "public, max-age=3600");
    server.send_P(200, "image/svg+xml", WebAssets::kLogoSvg);
  });
  server.on("/api/stats", HTTP_GET, [&server]() { sendStats(server); });
}
