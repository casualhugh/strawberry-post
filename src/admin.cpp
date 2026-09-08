#include "admin.h"

#include "app_config.h"
#include "device_time.h"
#include "diagnostics.h"
#include "generated_web_assets.h"
#include "letters.h"
#include "notices.h"
#include "storage.h"
#include "web_utils.h"

namespace {

LetterRecord letterPage[6];
NoticeRecord noticePage[8];

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
  const uint32_t letterBefore = server.arg("letterBefore").toInt();
  const uint32_t noticeBefore = server.arg("noticeBefore").toInt();
  bool moreLetters = false;
  bool moreNotices = false;
  const size_t letterPageCount =
      loadLetterPage(letterBefore, letterPage, 6, moreLetters);
  const size_t noticePageCount =
      loadNoticePage(noticeBefore, noticePage, 8, false, moreNotices);
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json; charset=utf-8", "");
  server.sendContent(F("{\"letters\":["));
  String response;
  response.reserve(1600);
  for (size_t index = 0; index < letterPageCount; ++index) {
    const LetterRecord* letter = &letterPage[index];
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
  for (size_t index = 0; index < noticePageCount; ++index) {
    const NoticeRecord* notice = &noticePage[index];
    response = "";
    if (index > 0) response += ',';
    response += F("{\"id\":");
    response += notice->id;
    response += ',';
    appendJsonField(response, "category",
                    noticeCategoryName(notice->category));
    response += ',';
    appendJsonField(response, "message", notice->message);
    response += F(",\"hidden\":");
    response += notice->hidden ? F("true}") : F("false}");
    server.sendContent(response);
  }
  response = F("],\"nextLetterBeforeId\":");
  response += moreLetters && letterPageCount
                  ? letterPage[letterPageCount - 1].id
                  : 0;
  response += F(",\"nextNoticeBeforeId\":");
  response += moreNotices && noticePageCount
                  ? noticePage[noticePageCount - 1].id
                  : 0;
  response += F(",\"deviceTimeSet\":");
  response += deviceTimeIsSet() ? F("true") : F("false");
  response += F(",\"deviceEpochSeconds\":");
  response += static_cast<unsigned long long>(deviceEpochSeconds());
  response += F(",\"noticeStorageBackend\":\"");
  response += storageBackendName(recordStorageBackend(RecordStorage::Notices));
  response += F("\",\"noticeStorageReadable\":");
  response += recordStorageReadable(RecordStorage::Notices) ? F("true")
                                                            : F("false");
  response += F(",\"noticeStorageWritable\":");
  response += recordStorageWritable(RecordStorage::Notices) ? F("true")
                                                            : F("false");
  response += F(",\"letterStorageBackend\":\"");
  response += storageBackendName(recordStorageBackend(RecordStorage::Letters));
  response += F("\",\"letterStorageReadable\":");
  response += recordStorageReadable(RecordStorage::Letters) ? F("true")
                                                            : F("false");
  response += F(",\"letterStorageWritable\":");
  response += recordStorageWritable(RecordStorage::Letters) ? F("true")
                                                            : F("false");
  response += F(",\"storageIssue\":\"");
  response += storageIssueName(storageIssue());
  response += '"';
  response += '}';
  server.sendContent(response);
}

bool parseEpochSeconds(const String& value, uint64_t& epochSeconds) {
  if (value.isEmpty() || value.length() > 12) return false;
  uint64_t parsed = 0;
  for (size_t index = 0; index < value.length(); ++index) {
    const char character = value[index];
    if (character < '0' || character > '9') return false;
    parsed = parsed * 10U + static_cast<uint8_t>(character - '0');
  }
  epochSeconds = parsed;
  return true;
}

void handleSetTime(WebServer& server) {
  recordHttpRequest(server);
  if (!authenticate(server)) return;
  if (!formRequestWithinLimits(server)) {
    sendError(server, 413, F("Request is too large"));
    return;
  }
  uint64_t epochSeconds = 0;
  if (!parseEpochSeconds(server.arg("epochSeconds"), epochSeconds) ||
      !setDeviceTime(epochSeconds)) {
    sendError(server, 400, F("A valid browser UTC time is required"));
    return;
  }
  const bool noticesUpdated = backfillNoticeCreationTimes();
  const bool lettersUpdated = backfillLetterCreationTimes();
  if (!noticesUpdated || !lettersUpdated) {
    sendError(server, 507,
              F("Clock set, but some pending record times could not be saved"));
    return;
  }
  server.send(200, "application/json", "{\"ok\":true}");
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
  server.on("/api/admin/time", HTTP_POST,
            [&server]() { handleSetTime(server); });
  server.on("/api/admin/letters/status", HTTP_POST,
            [&server]() { handleLetterStatus(server); });
  server.on("/api/admin/letters/delete", HTTP_POST,
            [&server]() { handleLetterDelete(server); });
  server.on("/api/admin/notices/moderate", HTTP_POST,
            [&server]() { handleNoticeModeration(server); });
}
