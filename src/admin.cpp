#include "admin.h"

#include "app_config.h"
#include "diagnostics.h"
#include "generated_web_assets.h"
#include "letters.h"
#include "notices.h"
#include "web_utils.h"

namespace {

bool authenticate(WebServer& server) {
  if (server.authenticate(AppConfig::kAdminUsername, AppConfig::kAdminPassword)) {
    return true;
  }
  server.requestAuthentication(BASIC_AUTH, "Strawberry Post Postie");
  return false;
}

void sendError(WebServer& server, int status, const __FlashStringHelper* message) {
  String body = F("{\"error\":\"");
  body += message;
  body += F("\"}");
  server.send(status, "application/json; charset=utf-8", body);
}

void appendJsonField(String& response, const char* name, const char* value) {
  response += '"';
  response += name;
  response += F("\":\"");
  response += escapeJson(value);
  response += '"';
}

void handleOverview(WebServer& server) {
  recordHttpRequest(server);
  if (!authenticate(server)) {
    return;
  }
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json; charset=utf-8", "");
  server.sendContent(F("{\"letters\":["));
  String response;
  response.reserve(1600);
  for (size_t index = 0; index < letterCount(); ++index) {
    const LetterRecord* letter = letterAt(index);
    response = "";
    if (index > 0) response += ',';
    response += F("{\"id\":");
    response += letter->id;
    response += ',';
    appendJsonField(response, "tracking", letter->trackingCode);
    response += ',';
    appendJsonField(response, "recipient", letter->recipient);
    response += ',';
    appendJsonField(response, "location", letter->location);
    response += ',';
    appendJsonField(response, "message", letter->message);
    response += ',';
    appendJsonField(response, "sender", letter->sender);
    response += ',';
    appendJsonField(response, "status", letterStatusName(letter->status));
    response += '}';
    server.sendContent(response);
  }
  server.sendContent(F("],\"notices\":["));
  for (size_t index = 0; index < storedNoticeCount(); ++index) {
    const NoticeRecord* notice = noticeAt(index);
    if (!notice) break;
    response = "";
    if (index > 0) response += ',';
    response += F("{\"id\":");
    response += notice->id;
    response += ',';
    appendJsonField(response, "category", notice->category);
    response += ',';
    appendJsonField(response, "message", notice->message);
    response += F(",\"hidden\":");
    response += notice->hidden ? F("true}") : F("false}");
    server.sendContent(response);
  }
  server.sendContent(F("]}"));
}

bool parseStatus(const String& value, LetterStatus& status) {
  if (value == "Written") status = LetterStatus::Written;
  else if (value == "OutForDelivery") status = LetterStatus::OutForDelivery;
  else if (value == "Delivered") status = LetterStatus::Delivered;
  else if (value == "CouldNotFind") status = LetterStatus::CouldNotFind;
  else return false;
  return true;
}

void handleLetterStatus(WebServer& server) {
  recordHttpRequest(server);
  if (!authenticate(server)) return;
  if (!formRequestWithinLimits(server)) {
    sendError(server, 413, F("Request is too large"));
    return;
  }
  LetterStatus status;
  const uint32_t id = server.arg("id").toInt();
  if (id == 0 || !parseStatus(server.arg("status"), status)) {
    sendError(server, 400, F("Valid letter id and status are required"));
    return;
  }
  if (!findLetterById(id)) {
    sendError(server, 404, F("Letter not found"));
    return;
  }
  if (!updateLetterStatus(id, status)) {
    sendError(server, 507, F("Could not save letter status"));
    return;
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleLetterDelete(WebServer& server) {
  recordHttpRequest(server);
  if (!authenticate(server)) return;
  if (!formRequestWithinLimits(server)) {
    sendError(server, 413, F("Request is too large"));
    return;
  }
  const uint32_t id = server.arg("id").toInt();
  if (id == 0) {
    sendError(server, 400, F("Valid letter id is required"));
    return;
  }
  if (!findLetterById(id)) {
    sendError(server, 404, F("Letter not found"));
    return;
  }
  if (!deleteLetter(id)) {
    sendError(server, 507, F("Could not delete letter"));
    return;
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleNoticeModeration(WebServer& server) {
  recordHttpRequest(server);
  if (!authenticate(server)) return;
  if (!formRequestWithinLimits(server)) {
    sendError(server, 413, F("Request is too large"));
    return;
  }
  const uint32_t id = server.arg("id").toInt();
  const String action = server.arg("action");
  bool success = false;
  if (action == "hide") success = setNoticeHidden(id, true);
  else if (action == "unhide") success = setNoticeHidden(id, false);
  else if (action == "delete") success = deleteNotice(id);
  else {
    sendError(server, 400, F("Invalid moderation action"));
    return;
  }
  if (!success) {
    sendError(server, 404, F("Notice not found or could not be saved"));
    return;
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

}  // namespace

void registerAdminRoutes(WebServer& server) {
  server.on("/postie", HTTP_GET, [&server]() {
    recordHttpRequest(server);
    if (authenticate(server)) {
      server.send_P(200, "text/html; charset=utf-8", WebAssets::kAdminPage);
    }
  });
  server.on("/postie/diagnostics", HTTP_GET, [&server]() {
    recordHttpRequest(server);
    if (authenticate(server)) {
      server.send_P(200, "text/html; charset=utf-8",
                    WebAssets::kDiagnosticsPage);
    }
  });
  server.on("/api/admin/diagnostics", HTTP_GET, [&server]() {
    recordHttpRequest(server);
    if (authenticate(server)) {
      sendDiagnosticsJson(server);
    }
  });
  server.on("/api/admin/overview", HTTP_GET,
            [&server]() { handleOverview(server); });
  server.on("/api/admin/letters/status", HTTP_POST,
            [&server]() { handleLetterStatus(server); });
  server.on("/api/admin/letters/delete", HTTP_POST,
            [&server]() { handleLetterDelete(server); });
  server.on("/api/admin/notices/moderate", HTTP_POST,
            [&server]() { handleNoticeModeration(server); });
}
