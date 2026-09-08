#include "notices.h"

#include <ArduinoJson.h>
#include <stdlib.h>
#include <string.h>

#include "device_time.h"
#include "diagnostics.h"
#include "storage.h"
#include "strawberry_core.h"
#include "web_utils.h"

namespace {

constexpr char kNoticesDirectory[] = "/notices";
constexpr char kMetadataPath[] = "/notices/index.json";
constexpr char kNoticeFormat[] = "strawberry-post-notice";
constexpr char kMetadataFormat[] = "strawberry-post-notice-index";
constexpr uint16_t kNoticeVersion = 1;
constexpr size_t kMaximumNoticeJsonBytes = 8U * 1024U;
constexpr size_t kMaximumMetadataJsonBytes = 2U * 1024U;
constexpr uint64_t kNoticeLifetimeSeconds = 48U * 60U * 60U;

constexpr const char* kAllowedCategories[] = {
    "General",          "Missed Connection", "Lost & Found",
    "Event / Schedule", "Ride Share",        "Help Wanted",
    "For Sale / Swap",
};
constexpr size_t kCategoryCount =
    sizeof(kAllowedCategories) / sizeof(kAllowedCategories[0]);

struct NoticeStore {
  uint16_t count;
  uint32_t nextId;
  uint32_t totalSubmitted;
  uint32_t retainedCount;
  NoticeRecord records[AppConfig::kMaxNotices];
};

NoticeStore store{};
bool loaded = false;
uint32_t lastSubmissionHash = 0;
uint32_t lastSubmissionAtMs = 0;
uint32_t lastSubmissionId = 0;
NoticeRecord responsePage[8];

bool validJsonText(JsonVariantConst value, size_t maximumBytes,
                   bool allowNewlines) {
  if (!value.is<const char*>()) return false;
  const char* text = value.as<const char*>();
  const size_t length = strlen(text);
  return length > 0 && length <= maximumBytes &&
         StrawberryCore::validUserText(text, length, allowNewlines);
}

bool noticeExpired(const NoticeRecord& notice) {
  return deviceTimeIsSet() &&
         StrawberryCore::recordAgeReached(
             deviceEpochSeconds(), notice.createdAtEpochSeconds,
             kNoticeLifetimeSeconds);
}

bool makeNoticePath(uint32_t id, char* destination, size_t destinationSize) {
  const int written = snprintf(destination, destinationSize,
                               "/notices/%010lu.json",
                               static_cast<unsigned long>(id));
  return written > 0 && static_cast<size_t>(written) < destinationSize;
}

uint32_t noticeIdFromPath(const char* path) {
  const char* name = strrchr(path, '/');
  name = name ? name + 1 : path;
  if (strlen(name) != 15 || strcmp(name + 10, ".json") != 0) return 0;
  uint32_t id = 0;
  for (size_t index = 0; index < 10; ++index) {
    if (name[index] < '0' || name[index] > '9') return 0;
    id = id * 10U + static_cast<uint32_t>(name[index] - '0');
  }
  return id;
}

bool serializeNotice(Print& output, void* context) {
  const NoticeRecord& notice = *static_cast<const NoticeRecord*>(context);
  JsonDocument document;
  document["format"] = kNoticeFormat;
  document["version"] = kNoticeVersion;
  document["id"] = notice.id;
  document["createdAtEpochSeconds"] = notice.createdAtEpochSeconds;
  document["createdAtUptimeMs"] = notice.createdAtUptimeMs;
  document["bootId"] = notice.bootId;
  document["hidden"] = notice.hidden != 0;
  document["category"] = noticeCategoryName(notice.category);
  document["message"] = notice.message;
  return !document.overflowed() && serializeJsonPretty(document, output) > 0;
}

bool deserializeNotice(Stream& input, void* context) {
  JsonDocument document;
  if (deserializeJson(document, input,
                      DeserializationOption::NestingLimit(2)) !=
          DeserializationError::Ok ||
      !document.is<JsonObject>()) {
    return false;
  }
  JsonObjectConst root = document.as<JsonObjectConst>();
  NoticeCategory category = NoticeCategory::General;
  if (root.size() != 9 || !root["format"].is<const char*>() ||
      strcmp(root["format"].as<const char*>(), kNoticeFormat) != 0 ||
      !root["version"].is<uint16_t>() ||
      root["version"].as<uint16_t>() != kNoticeVersion ||
      !root["id"].is<uint32_t>() || root["id"].as<uint32_t>() == 0 ||
      !root["createdAtEpochSeconds"].is<uint64_t>() ||
      !root["createdAtUptimeMs"].is<uint32_t>() ||
      !root["bootId"].is<uint32_t>() || !root["hidden"].is<bool>() ||
      !validJsonText(root["category"], AppConfig::kNoticeCategoryMaxBytes,
                     false) ||
      !parseNoticeCategory(root["category"].as<const char*>(), category) ||
      !validJsonText(root["message"], AppConfig::kNoticeMessageMaxBytes,
                     true)) {
    return false;
  }
  NoticeRecord& notice = *static_cast<NoticeRecord*>(context);
  notice = {};
  notice.id = root["id"].as<uint32_t>();
  notice.createdAtEpochSeconds = root["createdAtEpochSeconds"].as<uint64_t>();
  notice.createdAtUptimeMs = root["createdAtUptimeMs"].as<uint32_t>();
  notice.bootId = root["bootId"].as<uint32_t>();
  notice.hidden = root["hidden"].as<bool>() ? 1 : 0;
  notice.category = category;
  strlcpy(notice.message, root["message"].as<const char*>(),
          sizeof(notice.message));
  return true;
}

bool serializeMetadata(Print& output, void*) {
  JsonDocument document;
  document["format"] = kMetadataFormat;
  document["version"] = kNoticeVersion;
  document["nextId"] = store.nextId;
  document["totalSubmitted"] = store.totalSubmitted;
  document["retainedCount"] = store.retainedCount;
  return serializeJsonPretty(document, output) > 0;
}

bool deserializeMetadata(Stream& input, void*) {
  JsonDocument document;
  if (deserializeJson(document, input,
                      DeserializationOption::NestingLimit(2)) !=
          DeserializationError::Ok ||
      !document.is<JsonObject>()) {
    return false;
  }
  JsonObjectConst root = document.as<JsonObjectConst>();
  if (root.size() != 5 || !root["format"].is<const char*>() ||
      strcmp(root["format"].as<const char*>(), kMetadataFormat) != 0 ||
      !root["version"].is<uint16_t>() ||
      root["version"].as<uint16_t>() != kNoticeVersion ||
      !root["nextId"].is<uint32_t>() || root["nextId"].as<uint32_t>() == 0 ||
      !root["totalSubmitted"].is<uint32_t>() ||
      !root["retainedCount"].is<uint32_t>()) {
    return false;
  }
  store.nextId = root["nextId"].as<uint32_t>();
  store.totalSubmitted = root["totalSubmitted"].as<uint32_t>();
  store.retainedCount = root["retainedCount"].as<uint32_t>();
  return true;
}

bool writeNotice(const NoticeRecord& notice) {
  char path[48];
  return makeNoticePath(notice.id, path, sizeof(path)) &&
         writeStorageJsonAtomic(RecordStorage::Notices, path, serializeNotice,
                                const_cast<NoticeRecord*>(&notice));
}

bool readNotice(uint32_t id, NoticeRecord& notice) {
  char path[48];
  return makeNoticePath(id, path, sizeof(path)) &&
         readStorageJsonValidated(RecordStorage::Notices, path,
                                  kMaximumNoticeJsonBytes, deserializeNotice,
                                  &notice) == StorageLoadResult::Loaded &&
         notice.id == id;
}

bool writeMetadata() {
  return writeStorageJsonAtomic(RecordStorage::Notices, kMetadataPath,
                                serializeMetadata);
}

void sortNoticesDescending(NoticeRecord* records, size_t count) {
  for (size_t index = 1; index < count; ++index) {
    NoticeRecord value = records[index];
    size_t position = index;
    while (position > 0 && records[position - 1].id < value.id) {
      records[position] = records[position - 1];
      --position;
    }
    records[position] = value;
  }
}

void cacheNotice(const NoticeRecord& notice) {
  for (size_t index = 0; index < store.count; ++index) {
    if (store.records[index].id == notice.id) {
      store.records[index] = notice;
      return;
    }
  }
  if (store.count < AppConfig::kMaxNotices) {
    store.records[store.count++] = notice;
  } else {
    size_t oldest = 0;
    for (size_t index = 1; index < store.count; ++index) {
      if (store.records[index].id < store.records[oldest].id) oldest = index;
    }
    if (notice.id <= store.records[oldest].id) return;
    store.records[oldest] = notice;
  }
  sortNoticesDescending(store.records, store.count);
}

struct LoadContext {
  uint32_t maximumId;
  uint32_t retainedCount;
};

bool loadCachedNotice(const char* path, void* opaque) {
  const uint32_t id = noticeIdFromPath(path);
  if (id == 0) return true;
  NoticeRecord notice{};
  if (!readNotice(id, notice)) {
    Serial.printf("Ignoring invalid notice record %s.\n", path);
    return true;
  }
  LoadContext& context = *static_cast<LoadContext*>(opaque);
  if (id > context.maximumId) context.maximumId = id;
  ++context.retainedCount;
  cacheNotice(notice);
  return true;
}

struct PageContext {
  uint32_t beforeId;
  NoticeRecord* destination;
  size_t capacity;
  size_t count;
  size_t eligible;
  bool publicOnly;
};

void considerNoticeForPage(const NoticeRecord& notice, PageContext& page) {
  if ((page.beforeId != 0 && notice.id >= page.beforeId) ||
      (page.publicOnly && (notice.hidden || noticeExpired(notice)))) {
    return;
  }
  ++page.eligible;
  if (page.count < page.capacity) {
    page.destination[page.count++] = notice;
    return;
  }
  size_t oldest = 0;
  for (size_t index = 1; index < page.count; ++index) {
    if (page.destination[index].id < page.destination[oldest].id) oldest = index;
  }
  if (notice.id > page.destination[oldest].id) {
    page.destination[oldest] = notice;
  }
}

bool loadNoticeForPage(const char* path, void* opaque) {
  PageContext& page = *static_cast<PageContext*>(opaque);
  const uint32_t id = noticeIdFromPath(path);
  if (id == 0 || (page.beforeId != 0 && id >= page.beforeId)) return true;
  NoticeRecord notice{};
  if (!readNotice(id, notice)) {
    return recordStorageReadable(RecordStorage::Notices);
  }
  considerNoticeForPage(notice, page);
  return true;
}

void loadCachedNoticePage(PageContext& page) {
  page.count = 0;
  page.eligible = 0;
  for (size_t index = 0; index < store.count; ++index) {
    considerNoticeForPage(store.records[index], page);
  }
}

struct PublicCountContext {
  size_t count;
};

bool countPublicNoticeFile(const char* path, void* opaque) {
  const uint32_t id = noticeIdFromPath(path);
  if (id == 0) return true;
  NoticeRecord notice{};
  if (!readNotice(id, notice)) {
    return recordStorageReadable(RecordStorage::Notices);
  }
  if (!notice.hidden && !noticeExpired(notice)) {
    ++static_cast<PublicCountContext*>(opaque)->count;
  }
  return true;
}

struct BackfillContext {
  bool success;
  uint64_t epochSeconds;
  uint32_t uptimeMs;
};

bool backfillNoticeFile(const char* path, void* opaque) {
  const uint32_t id = noticeIdFromPath(path);
  if (id == 0) return true;
  NoticeRecord notice{};
  if (!readNotice(id, notice) || notice.createdAtEpochSeconds != 0) return true;
  BackfillContext& context = *static_cast<BackfillContext*>(opaque);
  notice.createdAtEpochSeconds = StrawberryCore::backfillCreationEpochSeconds(
      context.epochSeconds, context.uptimeMs, notice.createdAtUptimeMs,
      notice.bootId == persistedBootCount());
  if (!writeNotice(notice)) {
    context.success = false;
    return false;
  }
  cacheNotice(notice);
  return true;
}

void sendError(WebServer& server, int status, const __FlashStringHelper* message) {
  String body = F("{\"error\":\"");
  body += message;
  body += F("\"}");
  server.send(status, "application/json; charset=utf-8", body);
}

void appendNoticeJson(String& response, const NoticeRecord& notice) {
  response += F("{\"id\":");
  response += notice.id;
  response += F(",\"category\":\"");
  response += escapeJson(noticeCategoryName(notice.category));
  response += F("\",\"message\":\"");
  response += escapeJson(notice.message);
  response += F("\",\"createdAtEpochSeconds\":");
  response += static_cast<unsigned long long>(notice.createdAtEpochSeconds);
  response += '}';
}

void handleList(WebServer& server) {
  recordHttpRequest(server);
  const uint32_t beforeId = server.arg("before").toInt();
  bool hasMore = false;
  const size_t count =
      loadNoticePage(beforeId, responsePage, 8, true, hasMore);
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json; charset=utf-8", "");
  server.sendContent(F("{\"notices\":["));
  String response;
  response.reserve(AppConfig::kNoticeMessageMaxBytes + 180);
  for (size_t index = 0; index < count; ++index) {
    response = index == 0 ? "" : ",";
    appendNoticeJson(response, responsePage[index]);
    server.sendContent(response);
  }
  response = F("],\"totalSubmitted\":");
  response += store.totalSubmitted;
  response += F(",\"nextBeforeId\":");
  response += hasMore && count ? responsePage[count - 1].id : 0;
  response += '}';
  server.sendContent(response);
}

void handleCreate(WebServer& server) {
  recordHttpRequest(server);
  if (!formRequestWithinLimits(server)) {
    sendError(server, 413, F("Request is too large"));
    return;
  }
  if (!loaded || !recordStorageWritable(RecordStorage::Notices)) {
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
  NoticeCategory parsedCategory = NoticeCategory::General;
  if (!parseNoticeCategory(category.c_str(), parsedCategory)) {
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
  if (store.nextId == 0 || store.nextId == UINT32_MAX) {
    sendError(server, 503, F("No notice identifiers are available"));
    return;
  }

  NoticeRecord notice{};
  notice.id = store.nextId;
  notice.createdAtEpochSeconds = deviceEpochSeconds();
  notice.createdAtUptimeMs = millis();
  notice.bootId = persistedBootCount();
  notice.category = parsedCategory;
  strlcpy(notice.message, message.c_str(), sizeof(notice.message));
  if (!writeNotice(notice)) {
    sendError(server, 507, F("Could not save notice"));
    return;
  }

  ++store.nextId;
  ++store.totalSubmitted;
  ++store.retainedCount;
  if (!writeMetadata()) {
    Serial.println("Notice saved; its index will be rebuilt on the next boot.");
  }
  cacheNotice(notice);
  lastSubmissionHash = submissionHash;
  lastSubmissionAtMs = millis();
  lastSubmissionId = notice.id;
  String response = F("{\"id\":");
  response += notice.id;
  response += F("}");
  server.send(201, "application/json; charset=utf-8", response);
}

}  // namespace

const char* noticeCategoryName(NoticeCategory category) {
  const size_t index = static_cast<size_t>(category);
  return index < kCategoryCount ? kAllowedCategories[index] : "General";
}

bool parseNoticeCategory(const char* name, NoticeCategory& category) {
  if (name == nullptr) return false;
  for (size_t index = 0; index < kCategoryCount; ++index) {
    if (strcmp(name, kAllowedCategories[index]) == 0) {
      category = static_cast<NoticeCategory>(index);
      return true;
    }
  }
  return false;
}

bool startNotices() {
  store = {};
  store.nextId = 1;
  const StorageBackend backend = recordStorageBackend(RecordStorage::Notices);
  if (backend == StorageBackend::None) {
    Serial.println("Notice storage unavailable; notice mutations are disabled.");
    return false;
  }
  const StorageLoadResult metadata = readStorageJsonValidated(
      RecordStorage::Notices, kMetadataPath, kMaximumMetadataJsonBytes,
      deserializeMetadata);
  if (metadata == StorageLoadResult::Invalid) {
    Serial.println("Notice index is invalid; rebuilding it from record files.");
    store.nextId = 1;
    store.totalSubmitted = 0;
    store.retainedCount = 0;
  }
  LoadContext context{};
  visitStorageFiles(RecordStorage::Notices, kNoticesDirectory,
                    loadCachedNotice, &context);
  store.retainedCount = context.retainedCount;
  if (context.maximumId >= store.nextId && context.maximumId != UINT32_MAX) {
    store.nextId = context.maximumId + 1;
  }
  if (store.totalSubmitted < context.maximumId) {
    store.totalSubmitted = context.maximumId;
  }
  loaded = true;
  Serial.printf("Notice store ready on %s: %u cached, %lu retained.\n",
                storageBackendName(backend), static_cast<unsigned>(store.count),
                static_cast<unsigned long>(store.retainedCount));
  return true;
}

void registerNoticeRoutes(WebServer& server) {
  server.on("/api/notices", HTTP_GET, [&server]() { handleList(server); });
  server.on("/api/notices", HTTP_POST, [&server]() { handleCreate(server); });
}

size_t activeNoticeCount() {
  size_t count = 0;
  for (size_t index = 0; index < store.count; ++index) {
    if (!store.records[index].hidden && !noticeExpired(store.records[index])) {
      ++count;
    }
  }
  return count;
}

size_t publicNoticeCount() {
  PublicCountContext context{};
  if (visitStorageFiles(RecordStorage::Notices, kNoticesDirectory,
                        countPublicNoticeFile, &context)) {
    return context.count;
  }
  return activeNoticeCount();
}

uint32_t totalNoticesSubmitted() { return store.totalSubmitted; }

size_t storedNoticeCount() { return store.retainedCount; }

const NoticeRecord* noticeAt(size_t index) {
  return index < store.count ? &store.records[index] : nullptr;
}

const NoticeRecord* activeNoticeAt(size_t index) {
  size_t current = 0;
  for (size_t storedIndex = 0; storedIndex < store.count; ++storedIndex) {
    const NoticeRecord& notice = store.records[storedIndex];
    if (notice.hidden || noticeExpired(notice)) continue;
    if (current++ == index) return &notice;
  }
  return nullptr;
}

size_t loadNoticePage(uint32_t beforeId, NoticeRecord* destination,
                      size_t capacity, bool publicOnly, bool& hasMore) {
  hasMore = false;
  if (destination == nullptr || capacity == 0) return 0;
  PageContext context{beforeId, destination, capacity, 0, 0, publicOnly};
  if (!visitStorageFiles(RecordStorage::Notices, kNoticesDirectory,
                         loadNoticeForPage, &context)) {
    loadCachedNoticePage(context);
  }
  sortNoticesDescending(destination, context.count);
  hasMore = context.eligible > context.count;
  return context.count;
}

bool backfillNoticeCreationTimes() {
  if (!deviceTimeIsSet()) return false;
  if (recordStorageBackend(RecordStorage::Notices) == StorageBackend::None) {
    return true;
  }
  BackfillContext context{true, deviceEpochSeconds(), millis()};
  if (!visitStorageFiles(RecordStorage::Notices, kNoticesDirectory,
                         backfillNoticeFile, &context)) {
    return false;
  }
  return context.success;
}

bool setNoticeHidden(uint32_t id, bool hidden) {
  NoticeRecord notice{};
  if (!readNotice(id, notice)) return false;
  notice.hidden = hidden ? 1 : 0;
  if (!writeNotice(notice)) return false;
  cacheNotice(notice);
  return true;
}

bool deleteNotice(uint32_t id) {
  char path[48];
  NoticeRecord notice{};
  if (!makeNoticePath(id, path, sizeof(path)) || !readNotice(id, notice)) {
    return false;
  }
  if (!removeStorageFile(RecordStorage::Notices, path)) return false;
  for (size_t index = 0; index < store.count; ++index) {
    if (store.records[index].id != id) continue;
    for (size_t next = index + 1; next < store.count; ++next) {
      store.records[next - 1] = store.records[next];
    }
    --store.count;
    break;
  }
  if (store.retainedCount > 0) --store.retainedCount;
  if (!writeMetadata()) {
    Serial.println("Notice deleted but its index count could not be updated.");
  }
  return true;
}
