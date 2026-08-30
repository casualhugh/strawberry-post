#include "web_server.h"

#include <Arduino.h>
#include <WebServer.h>

#include "app_config.h"

namespace {

WebServer server(AppConfig::kHttpPort);

// Keep the public page in program flash so the basic site is part of the
// firmware image and remains available even if the data filesystem fails.
// If this grows, retain HTML/CSS as separate source assets and embed them into
// PROGMEM at build time instead of making LittleFS a runtime dependency.
constexpr char kHomePage[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Strawberry Post</title>
  <style>
    body{margin:0;background:#fff8e7;color:#642b22;font-family:system-ui,sans-serif}
    main{max-width:34rem;margin:15vh auto;padding:2rem;text-align:center}
    h1{color:#b3262d;font-size:2.4rem;margin-bottom:.6rem}
    p{font-size:1.15rem;line-height:1.5}
  </style>
</head>
<body><main><h1>Strawberry Post</h1><p>Strawberry Post is running.</p></main></body>
</html>
)HTML";

void handleHome() {
  server.send_P(200, "text/html; charset=utf-8", kHomePage);
}

void handleNotFound() {
  server.send(404, "text/plain; charset=utf-8", "Not found\n");
}

}  // namespace

void startWebServer() {
  server.on("/", HTTP_GET, handleHome);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server listening on port 80.");
}

void handleWebRequests() {
  server.handleClient();
}
