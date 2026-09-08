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

bool canonicalOrFallbackHost() {
  String host = server.hostHeader();
  host.toLowerCase();
  return host == "post.local" || host == "post.local:80" ||
         host == "192.168.4.1" || host == "192.168.4.1:80";
}

void redirectToHome() {
  recordHttpRequest(server);
  server.sendHeader("Location", AppConfig::kLocalUrl, true);
  server.sendHeader("Cache-Control", "no-store");
  server.send(302, "text/plain; charset=utf-8",
              "Strawberry Post is at http://post.local/\n");
}

void handleHome() {
  if (!canonicalOrFallbackHost()) {
    redirectToHome();
    return;
  }
  sendPublicHome(server);
}

void handleNotFound() {
  redirectToHome();
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

  // Requests made to captive-check hostnames are redirected to the canonical
  // local URL by handleHome. Requests already using post.local or the direct AP
  // fallback receive the page without a redirect loop.
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
