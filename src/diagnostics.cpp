#include "diagnostics.h"

#include <WiFi.h>

#include "app_config.h"
#include "device_time.h"
#include "letters.h"
#include "notices.h"
#include "storage.h"
#include "web_utils.h"

namespace {

uint32_t requestCount = 0;
uint32_t lastRequestAtMs = 0;
uint32_t lastSummaryAtMs = 0;
char lastRequestUri[96] = "none";
char lastRequestMethod[8] = "none";

const char* methodName(HTTPMethod method) {
  switch (method) {
    case HTTP_GET:
      return "GET";
    case HTTP_POST:
      return "POST";
    case HTTP_PUT:
      return "PUT";
    case HTTP_PATCH:
      return "PATCH";
    case HTTP_DELETE:
      return "DELETE";
    case HTTP_HEAD:
      return "HEAD";
    case HTTP_OPTIONS:
      return "OPTIONS";
    default:
      return "OTHER";
  }
}

void printSummary() {
  Serial.printf(
      "[health] clients=%u requests=%lu heap=%u minHeap=%u notices=%u "
      "letters=%u noticeStore=%s/%s letterStore=%s/%s storage=%llu/%llu\n",
      static_cast<unsigned>(WiFi.softAPgetStationNum()),
      static_cast<unsigned long>(requestCount), ESP.getFreeHeap(),
      ESP.getMinFreeHeap(), static_cast<unsigned>(activeNoticeCount()),
      static_cast<unsigned>(letterCount()),
      storageBackendName(recordStorageBackend(RecordStorage::Notices)),
      recordStorageWritable(RecordStorage::Notices) ? "rw" : "ro",
      storageBackendName(recordStorageBackend(RecordStorage::Letters)),
      recordStorageWritable(RecordStorage::Letters) ? "rw" : "ro",
      static_cast<unsigned long long>(storageUsedBytes()),
      static_cast<unsigned long long>(storageTotalBytes()));
}

}  // namespace

void recordHttpRequest(WebServer& server) {
  ++requestCount;
  lastRequestAtMs = millis();
  strlcpy(lastRequestUri, server.uri().c_str(), sizeof(lastRequestUri));
  for (char& character : lastRequestUri) {
    if (character == '\0') {
      break;
    }
    if (static_cast<uint8_t>(character) < 0x20 || character == 0x7f) {
      character = '?';
    }
  }
  strlcpy(lastRequestMethod, methodName(server.method()),
          sizeof(lastRequestMethod));
  if (AppConfig::kSerialRequestLogging) {
    Serial.printf("[http] #%lu %s %s clients=%u heap=%u\n",
                  static_cast<unsigned long>(requestCount), lastRequestMethod,
                  lastRequestUri,
                  static_cast<unsigned>(WiFi.softAPgetStationNum()),
                  ESP.getFreeHeap());
  }
}

void handlePeriodicDiagnostics() {
  const uint32_t now = millis();
  if (static_cast<uint32_t>(now - lastSummaryAtMs) <
      AppConfig::kDiagnosticsIntervalMs) {
    return;
  }
  lastSummaryAtMs = now;
  printSummary();
}

void sendDiagnosticsJson(WebServer& server) {
  String response = F("{\"uptimeSeconds\":");
  response += millis() / 1000UL;
  response += F(",\"apClients\":");
  response += WiFi.softAPgetStationNum();
  response += F(",\"requests\":");
  response += requestCount;
  response += F(",\"lastRequestAgeMs\":");
  response += requestCount == 0 ? 0 : static_cast<uint32_t>(millis() - lastRequestAtMs);
  response += F(",\"lastMethod\":\"");
  response += lastRequestMethod;
  response += F("\",\"lastUri\":\"");
  response += escapeJson(lastRequestUri);
  response += F("\",\"freeHeap\":");
  response += ESP.getFreeHeap();
  response += F(",\"minimumFreeHeap\":");
  response += ESP.getMinFreeHeap();
  response += F(",\"largestFreeBlock\":");
  response += ESP.getMaxAllocHeap();
  response += F(",\"deviceTimeSet\":");
  response += deviceTimeIsSet() ? F("true") : F("false");
  response += F(",\"deviceEpochSeconds\":");
  response += static_cast<unsigned long long>(deviceEpochSeconds());
  response += F(",\"storageAvailable\":");
  response += storageAvailable() ? F("true") : F("false");
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
  response += F(",\"storageUsedBytes\":");
  response += storageUsedBytes();
  response += F(",\"storageTotalBytes\":");
  response += storageTotalBytes();
  response += F(",\"activeNotices\":");
  response += activeNoticeCount();
  response += F(",\"storedNotices\":");
  response += storedNoticeCount();
  response += F(",\"letters\":");
  response += letterCount();
  response += F(",\"lettersWaiting\":");
  response += lettersWithStatus(LetterStatus::Waiting);
  response += F(",\"lettersOutForDelivery\":");
  response += lettersWithStatus(LetterStatus::OutForDelivery);
  response += F(",\"lettersDelivered\":");
  response += lettersWithStatus(LetterStatus::Delivered);
  response += '}';
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json; charset=utf-8", response);
}
