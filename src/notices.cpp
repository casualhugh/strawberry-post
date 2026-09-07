#include "notices.h"

#include <string.h>

#include "diagnostics.h"
#include "generated_web_assets.h"
#include "storage.h"
#include "storage_format.h"
#include "strawberry_core.h"
#include "web_utils.h"

namespace {

constexpr char kNoticesPath[] = "/notices.dat";
constexpr uint32_t kNoticesMagic = makeStorageMagic('N', 'O', 'T', 'C');
constexpr uint16_t kNoticesVersion = 1;
// Keep this list in sync with the category dropdown in web/index.html. The
// server validates it too so clients cannot create unexpected categories by
// bypassing the browser form.
constexpr const char* kAllowedCategories[] = {
    "General",          "Missed Connection", "Lost & Found",
    "Event / Schedule", "Ride Share",        "Help Wanted",
    "For Sale / Swap",
};

struct NoticeStore {
  uint32_t magic;
  uint16_t version;
  uint16_t count;
  uint32_t nextId;
  uint32_t totalSubmitted;
  NoticeRecord records[AppConfig::kMaxNotices];
};

NoticeStore store{};
bool loaded = false;
uint32_t lastSubmissionHash = 0;
uint32_t lastSubmissionAtMs = 0;
uint32_t lastSubmissionId = 0;

bool expired(const NoticeRecord& notice, uint32_t now) {
  return StrawberryCore::deadlineReached(now, notice.expiresAtMs);
}

bool allowedCategory(const String& category) {
  for (const char* allowed : kAllowedCategories) {
    if (category == allowed) {
      return true;
    }
  }
  return false;
}

bool persist() {
  return storageAvailable() &&
         writeStorageFileAtomic(kNoticesPath, &store, sizeof(store));
}

bool validNoticeStore(const void* data, size_t size, void*) {
  if (size != sizeof(NoticeStore)) {
    return false;
  }
  const NoticeStore& candidate = *static_cast<const NoticeStore*>(data);
  if (candidate.magic != kNoticesMagic ||
      candidate.version != kNoticesVersion ||
      candidate.count > AppConfig::kMaxNotices) {
    return false;
  }
  for (size_t index = 0; index < candidate.count; ++index) {
    const NoticeRecord& notice = candidate.records[index];
    if (notice.hidden > 1 ||
        !validStoredText(notice.category, sizeof(notice.category), false) ||
        !validStoredText(notice.message, sizeof(notice.message))) {
      return false;
    }
  }
  return true;
}

struct ExpiryContext {
  uint32_t nowMs;
};

bool shouldRemoveExpiredNotice(const void* record, void* context) {
  const NoticeRecord& notice = *static_cast<const NoticeRecord*>(record);
  const ExpiryContext& expiry = *static_cast<const ExpiryContext*>(context);
  return expired(notice, expiry.nowMs);
}

bool commitNoticeCompaction(void*) {
  return persist();
}

void removeExpired() {
  ExpiryContext context{millis()};
  StrawberryCore::compactRecordsTransactional(
      store.records, store.count, sizeof(store.records[0]),
      shouldRemoveExpiredNotice, &context, commitNoticeCompaction, nullptr);
}

void sendError(WebServer& server, int status, const __FlashStringHelper* message) {
  String body = F("{\"error\":\"");
  body += message;
  body += F("\"}");
  server.send(status, "application/json; charset=utf-8", body);
}

void handleList(WebServer& server) {
  recordHttpRequest(server);
  removeExpired();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json; charset=utf-8", "");
  server.sendContent(F("{\"notices\":["));
  String response;
  response.reserve(AppConfig::kNoticeMessageMaxBytes + 160);
  bool first = true;
  const uint32_t now = millis();
  for (size_t index = 0; index < store.count; ++index) {
    const NoticeRecord& notice = store.records[index];
    if (notice.hidden || expired(notice, now)) {
      continue;
    }
    response = "";
    if (!first) {
      response += ',';
    }
    first = false;
    response += F("{\"id\":");
    response += notice.id;
    response += F(",\"category\":\"");
    response += escapeJson(notice.category);
    response += F("\",\"message\":\"");
    response += escapeJson(notice.message);
    response += F("\",\"createdUptimeSeconds\":");
    response += notice.createdAtMs / 1000UL;
    response += F(",\"expiresInSeconds\":");
    response += (notice.expiresAtMs - now) / 1000UL;
    response += '}';
    server.sendContent(response);
  }
  response = F("],\"totalSubmitted\":");
  response += store.totalSubmitted;
  response += '}';
  server.sendContent(response);
}

void handleCreate(WebServer& server) {
  recordHttpRequest(server);
  removeExpired();
  if (!formRequestWithinLimits(server)) {
    sendError(server, 413, F("Request is too large"));
    return;
  }
  if (!loaded || !storageAvailable()) {
    sendError(server, 503, F("Persistent storage is unavailable"));
    return;
  }

  String category = server.arg("category");
  String message = server.arg("message");
  category.trim();
  message.trim();
  if (category.isEmpty() || message.isEmpty()) {
    sendError(server, 400, F("Category and message are required"));
    return;
  }
  if (!validUserText(category, false) || !validUserText(message)) {
    sendError(server, 400, F("Notice contains invalid text"));
    return;
  }
  if (!allowedCategory(category)) {
    sendError(server, 400, F("Choose a valid notice category"));
    return;
  }
  if (category.length() > AppConfig::kNoticeCategoryMaxBytes ||
      message.length() > AppConfig::kNoticeMessageMaxBytes) {
    sendError(server, 413, F("Notice is too long"));
    return;
  }
  uint32_t submissionHash = appendSubmissionHash(0, category);
  submissionHash = appendSubmissionHash(submissionHash, message);
  if (recentlySubmitted(submissionHash, lastSubmissionHash,
                        lastSubmissionAtMs)) {
    String response = F("{\"id\":");
    response += lastSubmissionId;
    response += F(",\"duplicate\":true}");
    server.send(200, "application/json; charset=utf-8", response);
    return;
  }

  bool pruned = false;
  NoticeRecord prunedRecord{};
  if (store.count >= AppConfig::kMaxNotices) {
    prunedRecord = store.records[0];
    for (size_t index = 1; index < store.count; ++index) {
      store.records[index - 1] = store.records[index];
    }
    --store.count;
    pruned = true;
  }

  NoticeRecord& notice = store.records[store.count];
  memset(&notice, 0, sizeof(notice));
  notice.id = store.nextId++;
  notice.createdAtMs = millis();
  notice.expiresAtMs = notice.createdAtMs + AppConfig::kPublicPostLifetimeMs;
  notice.bootId = persistedBootCount();
  strlcpy(notice.category, category.c_str(), sizeof(notice.category));
  strlcpy(notice.message, message.c_str(), sizeof(notice.message));
  ++store.count;
  ++store.totalSubmitted;

  if (!persist()) {
    --store.count;
    --store.totalSubmitted;
    --store.nextId;
    if (pruned) {
      for (size_t index = store.count; index > 0; --index) {
        store.records[index] = store.records[index - 1];
      }
      store.records[0] = prunedRecord;
      ++store.count;
    }
    sendError(server, 507, F("Could not save notice"));
    return;
  }

  lastSubmissionHash = submissionHash;
  lastSubmissionAtMs = millis();
  lastSubmissionId = notice.id;

  String response = F("{\"id\":");
  response += notice.id;
  response += F("}");
  server.send(201, "application/json; charset=utf-8", response);
}

}  // namespace

bool startNotices() {
  store = {};
  const bool valid = readStorageFileValidated(
      kNoticesPath, &store, sizeof(store), validNoticeStore);
  if (!valid) {
    store = {};
    store.magic = kNoticesMagic;
    store.version = kNoticesVersion;
    store.nextId = 1;
  }

  const uint32_t now = millis();
  bool changed = false;
  for (size_t index = 0; index < store.count; ++index) {
    NoticeRecord& notice = store.records[index];
    if (notice.bootId != persistedBootCount()) {
      notice.bootId = persistedBootCount();
      notice.createdAtMs = now;
      notice.expiresAtMs = now + AppConfig::kPublicPostLifetimeMs;
      changed = true;
    }
  }
  loaded = storageAvailable();
  if (changed && !persist()) {
    Serial.println("Failed to refresh notice expiry after reboot.");
  }
  Serial.printf("Notice store ready: %u active, %lu submitted.\n",
                static_cast<unsigned>(store.count),
                static_cast<unsigned long>(store.totalSubmitted));
  return loaded;
}

void registerNoticeRoutes(WebServer& server) {
  server.on("/api/notices", HTTP_GET, [&server]() { handleList(server); });
  server.on("/api/notices", HTTP_POST, [&server]() { handleCreate(server); });
}

size_t activeNoticeCount() {
  removeExpired();
  size_t count = 0;
  const uint32_t now = millis();
  for (size_t index = 0; index < store.count; ++index) {
    if (!store.records[index].hidden && !expired(store.records[index], now)) {
      ++count;
    }
  }
  return count;
}

uint32_t totalNoticesSubmitted() {
  return store.totalSubmitted;
}

size_t storedNoticeCount() {
  removeExpired();
  size_t count = 0;
  const uint32_t now = millis();
  for (size_t index = 0; index < store.count; ++index) {
    if (!expired(store.records[index], now)) {
      ++count;
    }
  }
  return count;
}

const NoticeRecord* noticeAt(size_t index) {
  removeExpired();
  const uint32_t now = millis();
  size_t current = 0;
  for (size_t storedIndex = 0; storedIndex < store.count; ++storedIndex) {
    if (expired(store.records[storedIndex], now)) {
      continue;
    }
    if (current == index) {
      return &store.records[storedIndex];
    }
    ++current;
  }
  return nullptr;
}

const NoticeRecord* activeNoticeAt(size_t index) {
  removeExpired();
  const uint32_t now = millis();
  size_t current = 0;
  for (size_t storedIndex = 0; storedIndex < store.count; ++storedIndex) {
    const NoticeRecord& notice = store.records[storedIndex];
    if (notice.hidden || expired(notice, now)) continue;
    if (current == index) return &notice;
    ++current;
  }
  return nullptr;
}

bool setNoticeHidden(uint32_t id, bool hidden) {
  for (size_t index = 0; index < store.count; ++index) {
    if (store.records[index].id == id) {
      const bool previous = store.records[index].hidden;
      store.records[index].hidden = hidden;
      if (persist()) {
        return true;
      }
      store.records[index].hidden = previous;
      return false;
    }
  }
  return false;
}

bool deleteNotice(uint32_t id) {
  for (size_t index = 0; index < store.count; ++index) {
    if (store.records[index].id == id) {
      const NoticeRecord removed = store.records[index];
      for (size_t next = index + 1; next < store.count; ++next) {
        store.records[next - 1] = store.records[next];
      }
      --store.count;
      if (persist()) {
        return true;
      }
      for (size_t previous = store.count; previous > index; --previous) {
        store.records[previous] = store.records[previous - 1];
      }
      store.records[index] = removed;
      ++store.count;
      return false;
    }
  }
  return false;
}
