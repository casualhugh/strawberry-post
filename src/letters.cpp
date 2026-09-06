#include "letters.h"

#include <esp_system.h>
#include <string.h>

#include "diagnostics.h"
#include "generated_web_assets.h"
#include "storage.h"
#include "storage_format.h"
#include "strawberry_core.h"
#include "web_utils.h"

namespace {

constexpr char kStorePath[] = "/letters.dat";
constexpr uint32_t kStoreMagic = makeStorageMagic('L', 'E', 'T', 'R');
constexpr uint16_t kStoreVersion = 1;

struct LetterStore {
  uint32_t magic;
  uint16_t version;
  uint16_t count;
  uint32_t nextId;
  uint32_t totalSubmitted;
  uint16_t nextTrackingNumber;
  uint16_t reserved;  // Reserved schema space; always zero in version 1.
  LetterRecord records[AppConfig::kMaxLetters];
};

LetterStore store{};
bool loaded = false;
uint32_t lastSubmissionHash = 0;
uint32_t lastSubmissionAtMs = 0;
char lastSubmissionTracking[AppConfig::kTrackingCodeMaxBytes + 1] = {};

bool persist() {
  // Letter records are private. Never serialize stale data from inactive slots
  // after a failed create or future pruning/deletion work.
  for (size_t index = store.count; index < AppConfig::kMaxLetters; ++index) {
    memset(&store.records[index], 0, sizeof(store.records[index]));
  }
  return storageAvailable() &&
         writeStorageFileAtomic(kStorePath, &store, sizeof(store));
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
    if (code[index] < '0' || code[index] > '9') {
      return false;
    }
  }
  return true;
}

bool validLetterStore(const void* data, size_t size, void*) {
  if (size != sizeof(LetterStore)) {
    return false;
  }
  const LetterStore& candidate = *static_cast<const LetterStore*>(data);
  if (candidate.magic != kStoreMagic || candidate.version != kStoreVersion ||
      candidate.count > AppConfig::kMaxLetters ||
      candidate.nextTrackingNumber >= StrawberryCore::kTrackingNumberLimit) {
    return false;
  }
  for (size_t index = 0; index < candidate.count; ++index) {
    const LetterRecord& letter = candidate.records[index];
    if (static_cast<uint8_t>(letter.status) >
            static_cast<uint8_t>(LetterStatus::CouldNotFind) ||
        !validTrackingCode(letter.trackingCode) ||
        !validStoredText(letter.recipient, sizeof(letter.recipient), false) ||
        !validStoredText(letter.location, sizeof(letter.location), false) ||
        !validStoredText(letter.message, sizeof(letter.message)) ||
        !validStoredText(letter.sender, sizeof(letter.sender), false)) {
      return false;
    }
    for (size_t previous = 0; previous < index; ++previous) {
      if (strcmp(candidate.records[previous].trackingCode,
                 letter.trackingCode) == 0) {
        return false;
      }
    }
  }
  return true;
}

bool trackingExists(const char* code, void*) {
  for (size_t index = 0; index < store.count; ++index) {
    if (strcmp(store.records[index].trackingCode, code) == 0) {
      return true;
    }
  }
  return false;
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
  if (!loaded || !storageAvailable()) {
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
  if (recipient.isEmpty() || location.isEmpty() || message.isEmpty()) {
    sendError(server, 400, F("Recipient, location and message are required"));
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

  bool pruned = false;
  size_t prunedIndex = 0;
  LetterRecord prunedRecord{};
  const uint16_t previousTrackingNumber = store.nextTrackingNumber;
  if (store.count >= AppConfig::kMaxLetters) {
    bool foundCompleted = false;
    for (size_t index = 0; index < store.count; ++index) {
      const LetterStatus status = store.records[index].status;
      if (status == LetterStatus::Delivered ||
          status == LetterStatus::CouldNotFind) {
        prunedIndex = index;
        foundCompleted = true;
        break;
      }
    }
    if (!foundCompleted) {
      sendError(server, 503, F("The Postie bag is full"));
      return;
    }
    prunedRecord = store.records[prunedIndex];
    for (size_t index = prunedIndex + 1; index < store.count; ++index) {
      store.records[index - 1] = store.records[index];
    }
    --store.count;
    pruned = true;
  }

  LetterRecord& letter = store.records[store.count];
  memset(&letter, 0, sizeof(letter));
  if (!generateTrackingCode(letter.trackingCode, sizeof(letter.trackingCode))) {
    store.nextTrackingNumber = previousTrackingNumber;
    if (pruned) {
      for (size_t index = store.count; index > prunedIndex; --index) {
        store.records[index] = store.records[index - 1];
      }
      store.records[prunedIndex] = prunedRecord;
      ++store.count;
    }
    sendError(server, 503, F("No tracking codes are available"));
    return;
  }
  letter.id = store.nextId++;
  letter.createdAtMs = millis();
  letter.bootId = persistedBootCount();
  letter.status = LetterStatus::Waiting;
  strlcpy(letter.recipient, recipient.c_str(), sizeof(letter.recipient));
  strlcpy(letter.location, location.c_str(), sizeof(letter.location));
  strlcpy(letter.message, message.c_str(), sizeof(letter.message));
  strlcpy(letter.sender, sender.c_str(), sizeof(letter.sender));
  ++store.count;
  ++store.totalSubmitted;

  if (!persist()) {
    --store.count;
    --store.totalSubmitted;
    --store.nextId;
    store.nextTrackingNumber = previousTrackingNumber;
    if (pruned) {
      for (size_t index = store.count; index > prunedIndex; --index) {
        store.records[index] = store.records[index - 1];
      }
      store.records[prunedIndex] = prunedRecord;
      ++store.count;
    } else {
      memset(&store.records[store.count], 0,
             sizeof(store.records[store.count]));
    }
    sendError(server, 507, F("Could not save letter"));
    return;
  }

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
  if (tracking.isEmpty() || tracking.length() > AppConfig::kTrackingCodeMaxBytes) {
    sendError(server, 400, F("A valid tracking code is required"));
    return;
  }
  if (!validUserText(tracking, false)) {
    sendError(server, 400, F("A valid tracking code is required"));
    return;
  }
  for (size_t index = 0; index < store.count; ++index) {
    const LetterRecord& letter = store.records[index];
    if (tracking.equals(letter.trackingCode)) {
      String response = F("{\"tracking\":\"");
      response += letter.trackingCode;
      response += F("\",\"status\":\"");
      response += letterStatusName(letter.status);
      response += F("\"}");
      server.send(200, "application/json; charset=utf-8", response);
      return;
    }
  }
  sendError(server, 404, F("Tracking code not found"));
}

}  // namespace

bool startLetters() {
  store = {};
  const bool valid = readStorageFileValidated(
      kStorePath, &store, sizeof(store), validLetterStore);
  if (!valid) {
    store = {};
    store.magic = kStoreMagic;
    store.version = kStoreVersion;
    store.nextId = 1;
    store.nextTrackingNumber =
        esp_random() % StrawberryCore::kTrackingNumberLimit;
  }
  loaded = storageAvailable();
  Serial.printf("Letter store ready: %u letters, %lu submitted.\n",
                static_cast<unsigned>(store.count),
                static_cast<unsigned long>(store.totalSubmitted));
  return loaded;
}

void registerLetterRoutes(WebServer& server) {
  server.on("/letters", HTTP_GET, [&server]() {
    recordHttpRequest(server);
    server.send_P(200, "text/html; charset=utf-8", WebAssets::kLettersPage);
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

size_t letterCount() {
  return store.count;
}

uint32_t totalLettersSubmitted() {
  return store.totalSubmitted;
}

size_t lettersWithStatus(LetterStatus status) {
  size_t count = 0;
  for (size_t index = 0; index < store.count; ++index) {
    if (store.records[index].status == status) {
      ++count;
    }
  }
  return count;
}

const LetterRecord* letterAt(size_t index) {
  return index < store.count ? &store.records[index] : nullptr;
}

const LetterRecord* findLetterById(uint32_t id) {
  for (size_t index = 0; index < store.count; ++index) {
    if (store.records[index].id == id) {
      return &store.records[index];
    }
  }
  return nullptr;
}

bool updateLetterStatus(uint32_t id, LetterStatus status) {
  for (size_t index = 0; index < store.count; ++index) {
    if (store.records[index].id == id) {
      const LetterStatus previous = store.records[index].status;
      store.records[index].status = status;
      if (persist()) {
        return true;
      }
      store.records[index].status = previous;
      return false;
    }
  }
  return false;
}
