#include "letters.h"

#include <ArduinoJson.h>
#include <esp_system.h>
#include <string.h>

#include "device_time.h"
#include "diagnostics.h"
#include "generated_web_assets.h"
#include "storage.h"
#include "strawberry_core.h"
#include "web_utils.h"

namespace {

constexpr char kLettersDirectory[] = "/letters";
constexpr char kMetadataPath[] = "/letters/index.json";
constexpr char kLetterFormat[] = "strawberry-post-letter";
constexpr char kMetadataFormat[] = "strawberry-post-letter-index";
constexpr uint16_t kLetterVersion = 1;
constexpr size_t kMaximumLetterJsonBytes = 12U * 1024U;
constexpr size_t kMaximumMetadataJsonBytes = 2U * 1024U;
constexpr size_t kStatusCount = 5;

struct LetterStore {
  uint16_t count;
  uint32_t nextId;
  uint32_t totalSubmitted;
  uint32_t retainedCount;
  uint16_t nextTrackingNumber;
  uint32_t statusCounts[kStatusCount];
  LetterRecord records[AppConfig::kMaxLetters];
};

LetterStore store{};
bool loaded = false;
uint32_t lastSubmissionHash = 0;
uint32_t lastSubmissionAtMs = 0;
char lastSubmissionTracking[AppConfig::kTrackingCodeMaxBytes + 1] = {};
LetterRecord lookupRecord{};

size_t statusIndex(LetterStatus status) {
  return static_cast<size_t>(status);
}

bool validTrackingCode(const char* code) {
  if (code == nullptr ||
      strnlen(code, StrawberryCore::kTrackingCodeBytes + 1) !=
          StrawberryCore::kTrackingCodeBytes ||
      memcmp(code, StrawberryCore::kTrackingPrefix,
             StrawberryCore::kTrackingPrefixBytes) != 0) {
    return false;
  }
  for (size_t index = StrawberryCore::kTrackingPrefixBytes;
       index < StrawberryCore::kTrackingCodeBytes; ++index) {
    if (code[index] < '0' || code[index] > '9') return false;
  }
  return true;
}

bool validLetterText(JsonVariantConst value, size_t maximumBytes,
                     bool allowNewlines, bool allowEmpty) {
  if (!value.is<const char*>()) return false;
  const char* text = value.as<const char*>();
  const size_t length = strlen(text);
  return (allowEmpty || length > 0) && length <= maximumBytes &&
         StrawberryCore::validUserText(text, length, allowNewlines);
}

bool parseLetterStatus(JsonVariantConst value, LetterStatus& status) {
  if (!value.is<const char*>()) return false;
  const char* name = value.as<const char*>();
  for (size_t index = 0; index < kStatusCount; ++index) {
    const LetterStatus candidate = static_cast<LetterStatus>(index);
    if (strcmp(name, letterStatusName(candidate)) == 0) {
      status = candidate;
      return true;
    }
  }
  return false;
}

bool makeLetterPath(const char* tracking, char* destination,
                    size_t destinationSize) {
  if (!validTrackingCode(tracking)) return false;
  const int written =
      snprintf(destination, destinationSize, "/letters/%s.json", tracking);
  return written > 0 && static_cast<size_t>(written) < destinationSize;
}

bool trackingFromPath(const char* path, char* tracking,
                      size_t trackingSize) {
  const char* name = strrchr(path, '/');
  name = name ? name + 1 : path;
  if (strlen(name) != StrawberryCore::kTrackingCodeBytes + 5 ||
      strcmp(name + StrawberryCore::kTrackingCodeBytes, ".json") != 0 ||
      trackingSize < StrawberryCore::kTrackingCodeBytes + 1) {
    return false;
  }
  memcpy(tracking, name, StrawberryCore::kTrackingCodeBytes);
  tracking[StrawberryCore::kTrackingCodeBytes] = '\0';
  return validTrackingCode(tracking);
}

bool serializeLetter(Print& output, void* context) {
  const LetterRecord& letter = *static_cast<const LetterRecord*>(context);
  JsonDocument document;
  document["format"] = kLetterFormat;
  document["version"] = kLetterVersion;
  document["id"] = letter.id;
  document["createdAtEpochSeconds"] = letter.createdAtEpochSeconds;
  document["createdAtUptimeMs"] = letter.createdAtUptimeMs;
  document["bootId"] = letter.bootId;
  document["status"] = letterStatusName(letter.status);
  document["trackingCode"] = letter.trackingCode;
  document["recipient"] = letter.recipient;
  document["location"] = letter.location;
  document["message"] = letter.message;
  document["sender"] = letter.sender;
  return !document.overflowed() && serializeJsonPretty(document, output) > 0;
}

bool deserializeLetter(Stream& input, void* context) {
  JsonDocument document;
  if (deserializeJson(document, input,
                      DeserializationOption::NestingLimit(2)) !=
          DeserializationError::Ok ||
      !document.is<JsonObject>()) {
    return false;
  }
  JsonObjectConst root = document.as<JsonObjectConst>();
  LetterStatus status = LetterStatus::Waiting;
  if (root.size() != 12 || !root["format"].is<const char*>() ||
      strcmp(root["format"].as<const char*>(), kLetterFormat) != 0 ||
      !root["version"].is<uint16_t>() ||
      root["version"].as<uint16_t>() != kLetterVersion ||
      !root["id"].is<uint32_t>() || root["id"].as<uint32_t>() == 0 ||
      !root["createdAtEpochSeconds"].is<uint64_t>() ||
      !root["createdAtUptimeMs"].is<uint32_t>() ||
      !root["bootId"].is<uint32_t>() ||
      !parseLetterStatus(root["status"], status) ||
      !validTrackingCode(root["trackingCode"].as<const char*>()) ||
      !validLetterText(root["recipient"],
                       AppConfig::kLetterRecipientMaxBytes, false, false) ||
      !validLetterText(root["location"], AppConfig::kLetterLocationMaxBytes,
                       false, true) ||
      !validLetterText(root["message"], AppConfig::kLetterMessageMaxBytes,
                       true, false) ||
      !validLetterText(root["sender"], AppConfig::kLetterSenderMaxBytes,
                       false, true)) {
    return false;
  }
  LetterRecord& letter = *static_cast<LetterRecord*>(context);
  letter = {};
  letter.id = root["id"].as<uint32_t>();
  letter.createdAtEpochSeconds = root["createdAtEpochSeconds"].as<uint64_t>();
  letter.createdAtUptimeMs = root["createdAtUptimeMs"].as<uint32_t>();
  letter.bootId = root["bootId"].as<uint32_t>();
  letter.status = status;
  strlcpy(letter.trackingCode, root["trackingCode"].as<const char*>(),
          sizeof(letter.trackingCode));
  strlcpy(letter.recipient, root["recipient"].as<const char*>(),
          sizeof(letter.recipient));
  strlcpy(letter.location, root["location"].as<const char*>(),
          sizeof(letter.location));
  strlcpy(letter.message, root["message"].as<const char*>(),
          sizeof(letter.message));
  strlcpy(letter.sender, root["sender"].as<const char*>(),
          sizeof(letter.sender));
  return true;
}

bool serializeMetadata(Print& output, void*) {
  JsonDocument document;
  document["format"] = kMetadataFormat;
  document["version"] = kLetterVersion;
  document["nextId"] = store.nextId;
  document["totalSubmitted"] = store.totalSubmitted;
  document["retainedCount"] = store.retainedCount;
  document["nextTrackingNumber"] = store.nextTrackingNumber;
  JsonArray counts = document["statusCounts"].to<JsonArray>();
  for (size_t index = 0; index < kStatusCount; ++index) {
    counts.add(store.statusCounts[index]);
  }
  return serializeJsonPretty(document, output) > 0;
}

bool deserializeMetadata(Stream& input, void*) {
  JsonDocument document;
  if (deserializeJson(document, input,
                      DeserializationOption::NestingLimit(3)) !=
          DeserializationError::Ok ||
      !document.is<JsonObject>()) {
    return false;
  }
  JsonObjectConst root = document.as<JsonObjectConst>();
  JsonVariantConst countsValue = root["statusCounts"];
  if (root.size() != 7 || !root["format"].is<const char*>() ||
      strcmp(root["format"].as<const char*>(), kMetadataFormat) != 0 ||
      !root["version"].is<uint16_t>() ||
      root["version"].as<uint16_t>() != kLetterVersion ||
      !root["nextId"].is<uint32_t>() || root["nextId"].as<uint32_t>() == 0 ||
      !root["totalSubmitted"].is<uint32_t>() ||
      !root["retainedCount"].is<uint32_t>() ||
      !root["nextTrackingNumber"].is<uint16_t>() ||
      root["nextTrackingNumber"].as<uint16_t>() >=
          StrawberryCore::kTrackingNumberLimit ||
      !countsValue.is<JsonArrayConst>() ||
      countsValue.as<JsonArrayConst>().size() != kStatusCount) {
    return false;
  }
  for (JsonVariantConst value : countsValue.as<JsonArrayConst>()) {
    if (!value.is<uint32_t>()) return false;
  }
  store.nextId = root["nextId"].as<uint32_t>();
  store.totalSubmitted = root["totalSubmitted"].as<uint32_t>();
  store.retainedCount = root["retainedCount"].as<uint32_t>();
  store.nextTrackingNumber = root["nextTrackingNumber"].as<uint16_t>();
  for (size_t index = 0; index < kStatusCount; ++index) {
    store.statusCounts[index] = countsValue[index].as<uint32_t>();
  }
  return true;
}

bool writeLetter(const LetterRecord& letter) {
  char path[48];
  return makeLetterPath(letter.trackingCode, path, sizeof(path)) &&
         writeStorageJsonAtomic(RecordStorage::Letters, path, serializeLetter,
                                const_cast<LetterRecord*>(&letter));
}

bool readLetter(const char* tracking, LetterRecord& letter) {
  char path[48];
  return makeLetterPath(tracking, path, sizeof(path)) &&
         readStorageJsonValidated(RecordStorage::Letters, path,
                                  kMaximumLetterJsonBytes, deserializeLetter,
                                  &letter) == StorageLoadResult::Loaded &&
         strcmp(letter.trackingCode, tracking) == 0;
}

bool writeMetadata() {
  return writeStorageJsonAtomic(RecordStorage::Letters, kMetadataPath,
                                serializeMetadata);
}

void sortLettersDescending(LetterRecord* records, size_t count) {
  for (size_t index = 1; index < count; ++index) {
    LetterRecord value = records[index];
    size_t position = index;
    while (position > 0 && records[position - 1].id < value.id) {
      records[position] = records[position - 1];
      --position;
    }
    records[position] = value;
  }
}

void cacheLetter(const LetterRecord& letter) {
  for (size_t index = 0; index < store.count; ++index) {
    if (store.records[index].id == letter.id) {
      store.records[index] = letter;
      return;
    }
  }
  if (store.count < AppConfig::kMaxLetters) {
    store.records[store.count++] = letter;
  } else {
    size_t oldest = 0;
    for (size_t index = 1; index < store.count; ++index) {
      if (store.records[index].id < store.records[oldest].id) oldest = index;
    }
    if (letter.id <= store.records[oldest].id) return;
    store.records[oldest] = letter;
  }
  sortLettersDescending(store.records, store.count);
}

bool trackingExists(const char* code, void*) {
  char path[48];
  return makeLetterPath(code, path, sizeof(path)) &&
         storageFileExists(RecordStorage::Letters, path);
}

bool generateTrackingCode(char* destination, size_t destinationSize) {
  uint16_t nextNumber = store.nextTrackingNumber;
  if (!StrawberryCore::allocateTrackingCode(
          store.nextTrackingNumber, destination, destinationSize,
          trackingExists, nullptr, nextNumber)) {
    return false;
  }
  store.nextTrackingNumber = nextNumber;
  return true;
}

struct LoadContext {
  uint32_t maximumId;
  uint32_t retainedCount;
  uint32_t statusCounts[kStatusCount];
};

bool loadCachedLetter(const char* path, void* opaque) {
  char tracking[AppConfig::kTrackingCodeMaxBytes + 1];
  if (!trackingFromPath(path, tracking, sizeof(tracking))) return true;
  LetterRecord letter{};
  if (!readLetter(tracking, letter)) {
    Serial.printf("Ignoring invalid letter record %s.\n", path);
    return true;
  }
  LoadContext& context = *static_cast<LoadContext*>(opaque);
  if (letter.id > context.maximumId) context.maximumId = letter.id;
  ++context.retainedCount;
  ++context.statusCounts[statusIndex(letter.status)];
  cacheLetter(letter);
  return true;
}

struct PageContext {
  uint32_t beforeId;
  LetterRecord* destination;
  size_t capacity;
  size_t count;
  size_t eligible;
};

void considerLetterForPage(const LetterRecord& letter, PageContext& page) {
  if (page.beforeId != 0 && letter.id >= page.beforeId) return;
  ++page.eligible;
  if (page.count < page.capacity) {
    page.destination[page.count++] = letter;
    return;
  }
  size_t oldest = 0;
  for (size_t index = 1; index < page.count; ++index) {
    if (page.destination[index].id < page.destination[oldest].id) oldest = index;
  }
  if (letter.id > page.destination[oldest].id) page.destination[oldest] = letter;
}

bool loadLetterForPage(const char* path, void* opaque) {
  PageContext& page = *static_cast<PageContext*>(opaque);
  char tracking[AppConfig::kTrackingCodeMaxBytes + 1];
  if (!trackingFromPath(path, tracking, sizeof(tracking))) return true;
  LetterRecord letter{};
  if (!readLetter(tracking, letter)) {
    return recordStorageReadable(RecordStorage::Letters);
  }
  considerLetterForPage(letter, page);
  return true;
}

void loadCachedLetterPage(PageContext& page) {
  page.count = 0;
  page.eligible = 0;
  for (size_t index = 0; index < store.count; ++index) {
    considerLetterForPage(store.records[index], page);
  }
}

struct FindContext {
  uint32_t id;
  bool found;
};

bool findLetterFile(const char* path, void* opaque) {
  FindContext& context = *static_cast<FindContext*>(opaque);
  if (context.found) return true;
  char tracking[AppConfig::kTrackingCodeMaxBytes + 1];
  if (!trackingFromPath(path, tracking, sizeof(tracking))) return true;
  LetterRecord candidate{};
  if (readLetter(tracking, candidate) && candidate.id == context.id) {
    lookupRecord = candidate;
    context.found = true;
  }
  return true;
}

struct BackfillContext {
  bool success;
  uint64_t epochSeconds;
  uint32_t uptimeMs;
};

bool backfillLetterFile(const char* path, void* opaque) {
  char tracking[AppConfig::kTrackingCodeMaxBytes + 1];
  if (!trackingFromPath(path, tracking, sizeof(tracking))) return true;
  LetterRecord letter{};
  if (!readLetter(tracking, letter) || letter.createdAtEpochSeconds != 0) {
    return true;
  }
  BackfillContext& context = *static_cast<BackfillContext*>(opaque);
  letter.createdAtEpochSeconds = StrawberryCore::backfillCreationEpochSeconds(
      context.epochSeconds, context.uptimeMs, letter.createdAtUptimeMs,
      letter.bootId == persistedBootCount());
  if (!writeLetter(letter)) {
    context.success = false;
    return false;
  }
  cacheLetter(letter);
  return true;
}

void sendError(WebServer& server, int status, const __FlashStringHelper* message) {
  String body = F("{\"error\":\"");
  body += message;
  body += F("\"}");
  server.send(status, "application/json; charset=utf-8", body);
}

void handleCreate(WebServer& server) {
  recordHttpRequest(server);
  if (!formRequestWithinLimits(server)) {
    sendError(server, 413, F("Request is too large"));
    return;
  }
  if (!loaded || !recordStorageWritable(RecordStorage::Letters)) {
    sendError(server, 503, F("Persistent storage is unavailable"));
    return;
  }
  String recipient = server.arg("recipient");
  String location = server.arg("location");
  String message = server.arg("message");
  String sender = server.arg("sender");
  recipient.trim();
  location.trim();
  message.trim();
  sender.trim();
  if (recipient.isEmpty() || message.isEmpty()) {
    sendError(server, 400, F("Recipient and message are required"));
    return;
  }
  if (!validUserText(recipient, false) || !validUserText(location, false) ||
      !validUserText(message) || !validUserText(sender, false)) {
    sendError(server, 400, F("Letter contains invalid text"));
    return;
  }
  if (recipient.length() > AppConfig::kLetterRecipientMaxBytes ||
      location.length() > AppConfig::kLetterLocationMaxBytes ||
      message.length() > AppConfig::kLetterMessageMaxBytes ||
      sender.length() > AppConfig::kLetterSenderMaxBytes) {
    sendError(server, 413, F("Letter is too long"));
    return;
  }
  uint32_t submissionHash = appendSubmissionHash(0, recipient);
  submissionHash = appendSubmissionHash(submissionHash, location);
  submissionHash = appendSubmissionHash(submissionHash, message);
  submissionHash = appendSubmissionHash(submissionHash, sender);
  if (recentlySubmitted(submissionHash, lastSubmissionHash,
                        lastSubmissionAtMs)) {
    String response = F("{\"tracking\":\"");
    response += lastSubmissionTracking;
    response += F("\",\"status\":\"Waiting\",\"duplicate\":true}");
    server.send(200, "application/json; charset=utf-8", response);
    return;
  }
  if (store.nextId == 0 || store.nextId == UINT32_MAX) {
    sendError(server, 503, F("No letter identifiers are available"));
    return;
  }

  LetterRecord letter{};
  const uint16_t previousTrackingNumber = store.nextTrackingNumber;
  if (!generateTrackingCode(letter.trackingCode, sizeof(letter.trackingCode))) {
    sendError(server, 503, F("No tracking codes are available"));
    return;
  }
  letter.id = store.nextId;
  letter.createdAtEpochSeconds = deviceEpochSeconds();
  letter.createdAtUptimeMs = millis();
  letter.bootId = persistedBootCount();
  letter.status = LetterStatus::Waiting;
  strlcpy(letter.recipient, recipient.c_str(), sizeof(letter.recipient));
  strlcpy(letter.location, location.c_str(), sizeof(letter.location));
  strlcpy(letter.message, message.c_str(), sizeof(letter.message));
  strlcpy(letter.sender, sender.c_str(), sizeof(letter.sender));
  if (!writeLetter(letter)) {
    store.nextTrackingNumber = previousTrackingNumber;
    sendError(server, 507, F("Could not save letter"));
    return;
  }

  ++store.nextId;
  ++store.totalSubmitted;
  ++store.retainedCount;
  ++store.statusCounts[statusIndex(LetterStatus::Waiting)];
  if (!writeMetadata()) {
    Serial.println("Letter saved; its index will be rebuilt on the next boot.");
  }
  cacheLetter(letter);
  lastSubmissionHash = submissionHash;
  lastSubmissionAtMs = millis();
  strlcpy(lastSubmissionTracking, letter.trackingCode,
          sizeof(lastSubmissionTracking));
  String response = F("{\"tracking\":\"");
  response += letter.trackingCode;
  response += F("\",\"status\":\"Waiting\"}");
  server.send(201, "application/json; charset=utf-8", response);
}

void handleStatus(WebServer& server) {
  recordHttpRequest(server);
  String tracking = server.arg("tracking");
  tracking.trim();
  tracking.toUpperCase();
  if (tracking.isEmpty() || tracking.length() > AppConfig::kTrackingCodeMaxBytes ||
      !validUserText(tracking, false)) {
    sendError(server, 400, F("A valid tracking code is required"));
    return;
  }
  LetterRecord letter{};
  if (!readLetter(tracking.c_str(), letter)) {
    bool foundInCache = false;
    for (size_t index = 0; index < store.count; ++index) {
      if (strcmp(store.records[index].trackingCode, tracking.c_str()) == 0) {
        letter = store.records[index];
        foundInCache = true;
        break;
      }
    }
    if (!foundInCache) {
      sendError(server, 404, F("Tracking code not found"));
      return;
    }
  }
  String response = F("{\"tracking\":\"");
  response += letter.trackingCode;
  response += F("\",\"status\":\"");
  response += letterStatusName(letter.status);
  response += F("\"}");
  server.send(200, "application/json; charset=utf-8", response);
}

}  // namespace

bool startLetters() {
  store = {};
  store.nextId = 1;
  store.nextTrackingNumber =
      esp_random() % StrawberryCore::kTrackingNumberLimit;
  const StorageBackend backend = recordStorageBackend(RecordStorage::Letters);
  if (backend == StorageBackend::None) {
    Serial.println("Letter storage unavailable; letter mutations are disabled.");
    return false;
  }
  const StorageLoadResult metadata = readStorageJsonValidated(
      RecordStorage::Letters, kMetadataPath, kMaximumMetadataJsonBytes,
      deserializeMetadata);
  if (metadata == StorageLoadResult::Invalid) {
    Serial.println("Letter index is invalid; rebuilding it from record files.");
    store.nextId = 1;
    store.totalSubmitted = 0;
    store.retainedCount = 0;
    memset(store.statusCounts, 0, sizeof(store.statusCounts));
  }
  LoadContext context{};
  visitStorageFiles(RecordStorage::Letters, kLettersDirectory,
                    loadCachedLetter, &context);
  store.retainedCount = context.retainedCount;
  memcpy(store.statusCounts, context.statusCounts, sizeof(store.statusCounts));
  if (context.maximumId >= store.nextId && context.maximumId != UINT32_MAX) {
    store.nextId = context.maximumId + 1;
  }
  if (store.totalSubmitted < context.maximumId) {
    store.totalSubmitted = context.maximumId;
  }
  loaded = true;
  Serial.printf("Letter store ready on %s: %u cached, %lu retained.\n",
                storageBackendName(backend), static_cast<unsigned>(store.count),
                static_cast<unsigned long>(store.retainedCount));
  return true;
}

void registerLetterRoutes(WebServer& server) {
  server.on("/letters", HTTP_GET, [&server]() {
    recordHttpRequest(server);
    server.send_P(200, "text/html; charset=utf-8", WebAssets::kLettersPage);
  });
  server.on("/track", HTTP_GET, [&server]() {
    recordHttpRequest(server);
    server.send_P(200, "text/html; charset=utf-8", WebAssets::kTrackingPage);
  });
  server.on("/api/letters", HTTP_POST, [&server]() { handleCreate(server); });
  server.on("/api/letters/status", HTTP_GET,
            [&server]() { handleStatus(server); });
}

const char* letterStatusName(LetterStatus status) {
  switch (status) {
    case LetterStatus::Waiting:
      return "Waiting";
    case LetterStatus::Written:
      return "Written";
    case LetterStatus::OutForDelivery:
      return "OutForDelivery";
    case LetterStatus::Delivered:
      return "Delivered";
    case LetterStatus::CouldNotFind:
      return "CouldNotFind";
  }
  return "Waiting";
}

size_t letterCount() { return store.retainedCount; }

uint32_t totalLettersSubmitted() { return store.totalSubmitted; }

size_t lettersWithStatus(LetterStatus status) {
  const size_t index = statusIndex(status);
  return index < kStatusCount ? store.statusCounts[index] : 0;
}

const LetterRecord* letterAt(size_t index) {
  return index < store.count ? &store.records[index] : nullptr;
}

const LetterRecord* findLetterById(uint32_t id) {
  for (size_t index = 0; index < store.count; ++index) {
    if (store.records[index].id == id) return &store.records[index];
  }
  FindContext context{id, false};
  visitStorageFiles(RecordStorage::Letters, kLettersDirectory, findLetterFile,
                    &context);
  return context.found ? &lookupRecord : nullptr;
}

size_t loadLetterPage(uint32_t beforeId, LetterRecord* destination,
                      size_t capacity, bool& hasMore) {
  hasMore = false;
  if (destination == nullptr || capacity == 0) return 0;
  PageContext context{beforeId, destination, capacity, 0, 0};
  if (!visitStorageFiles(RecordStorage::Letters, kLettersDirectory,
                         loadLetterForPage, &context)) {
    loadCachedLetterPage(context);
  }
  sortLettersDescending(destination, context.count);
  hasMore = context.eligible > context.count;
  return context.count;
}

bool backfillLetterCreationTimes() {
  if (!deviceTimeIsSet()) return false;
  if (recordStorageBackend(RecordStorage::Letters) == StorageBackend::None) {
    return true;
  }
  BackfillContext context{true, deviceEpochSeconds(), millis()};
  if (!visitStorageFiles(RecordStorage::Letters, kLettersDirectory,
                         backfillLetterFile, &context)) {
    return false;
  }
  return context.success;
}

bool updateLetterStatus(uint32_t id, LetterStatus status) {
  const LetterRecord* found = findLetterById(id);
  if (!found) return false;
  LetterRecord updated = *found;
  const LetterStatus previous = updated.status;
  updated.status = status;
  if (!writeLetter(updated)) return false;
  if (previous != status) {
    --store.statusCounts[statusIndex(previous)];
    ++store.statusCounts[statusIndex(status)];
    if (!writeMetadata()) {
      Serial.println(
          "Letter status saved; index counts will be rebuilt on next boot.");
    }
  }
  cacheLetter(updated);
  return true;
}

bool deleteLetter(uint32_t id) {
  const LetterRecord* found = findLetterById(id);
  if (!found) return false;
  const LetterRecord removed = *found;
  char path[48];
  if (!makeLetterPath(removed.trackingCode, path, sizeof(path)) ||
      !removeStorageFile(RecordStorage::Letters, path)) {
    return false;
  }
  for (size_t index = 0; index < store.count; ++index) {
    if (store.records[index].id != id) continue;
    for (size_t next = index + 1; next < store.count; ++next) {
      store.records[next - 1] = store.records[next];
    }
    --store.count;
    break;
  }
  if (store.retainedCount > 0) --store.retainedCount;
  if (store.statusCounts[statusIndex(removed.status)] > 0) {
    --store.statusCounts[statusIndex(removed.status)];
  }
  if (!writeMetadata()) {
    Serial.println("Letter deleted but its index counts could not be updated.");
  }
  if (strcmp(lastSubmissionTracking, removed.trackingCode) == 0) {
    lastSubmissionHash = 0;
    lastSubmissionAtMs = 0;
    lastSubmissionTracking[0] = '\0';
  }
  return true;
}
