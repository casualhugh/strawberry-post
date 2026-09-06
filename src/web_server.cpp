#include "web_server.h"

#include <Arduino.h>
#include <WebServer.h>

#include "admin.h"
#include "app_config.h"
#include "diagnostics.h"
#include "letters.h"
#include "notices.h"
#include "public_ui.h"

namespace {

WebServer server(AppConfig::kHttpPort);

void handleHome() {
  sendPublicHome(server);
}

void handleNotFound() {
  recordHttpRequest(server);
  server.sendHeader("Location", AppConfig::kLocalUrl, true);
  server.send(302, "text/plain; charset=utf-8",
              "Strawberry Post is at http://post.local/\n");
}

}  // namespace

void startWebServer() {
  static const char* kCollectedHeaders[] = {"Content-Length"};
  server.collectHeaders(kCollectedHeaders, 1);
  server.on("/", HTTP_GET, handleHome);
  registerPublicUiRoutes(server);
  registerNoticeRoutes(server);
  registerLetterRoutes(server);
  registerAdminRoutes(server);

  // Return unexpected content for common operating-system connectivity checks.
  // This may prompt a captive-network window, but users can always browse to
  // post.local or the AP address directly if their phone does not show one.
  server.on("/generate_204", HTTP_ANY, handleHome);          // Android
  server.on("/gen_204", HTTP_ANY, handleHome);               // Android
  server.on("/hotspot-detect.html", HTTP_ANY, handleHome);   // Apple
  server.on("/library/test/success.html", HTTP_ANY,
            handleHome);                                    // Apple
  server.on("/ncsi.txt", HTTP_ANY, handleHome);              // Windows
  server.on("/connecttest.txt", HTTP_ANY, handleHome);       // Windows
  server.on("/redirect", HTTP_ANY, handleHome);              // Windows

  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server listening on port 80.");
}

void handleWebRequests() {
  server.handleClient();
}
