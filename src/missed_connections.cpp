#include "missed_connections.h"

#include <string.h>

#include "storage.h"
#include "web_utils.h"

namespace {

constexpr char kStorePath[] = "/missed.dat";
constexpr uint32_t kStoreMagic = 0x4d495353;  // "MISS"
constexpr uint16_t kStoreVersion = 2;

struct MissedStore {
  uint32_t magic;
  uint16_t version;
  uint16_t count;
  uint32_t nextId;
  uint32_t totalSubmitted;
  MissedConnectionRecord records[AppConfig::kMaxMissedConnections];
};

MissedStore store{};
bool loaded = false;
uint32_t lastSubmissionHash = 0;
uint32_t lastSubmissionAtMs = 0;
uint32_t lastSubmissionId = 0;

constexpr char kMissedPage[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Missed Connections | Strawberry Post</title><link rel="stylesheet" href="/style.css"></head><body><main><a class="back" href="/">&larr; Strawberry Post</a><h1>Missed Connections</h1><p class="hint">For ships that passed in the festival night. Posts stay up for about eight hours.</p>
<form id="form"><label>To / title<input name="title" maxlength="80" required></label><label>Message<textarea name="message" maxlength="280" required></textarea></label>
<button>Post connection</button></form><p id="result" role="status"></p><section id="posts"></section><script>
const form=document.querySelector('#form'),result=document.querySelector('#result'),posts=document.querySelector('#posts');
async function load(){const r=await fetch('/api/missed');const data=await r.json();posts.replaceChildren(...data.missed.map(n=>{const article=document.createElement('article');article.className='post';const strong=document.createElement('strong');strong.textContent=n.title;const p=document.createElement('p');p.textContent=n.message;article.append(strong,p);return article}))}
form.addEventListener('submit',async e=>{e.preventDefault();result.textContent='Posting...';const r=await fetch('/api/missed',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(new FormData(form))});const data=await r.json();result.textContent=data.error||'Connection posted.';if(r.ok){form.reset();load()}});load();
</script></main></body></html>
)HTML";

bool expired(const MissedConnectionRecord& record, uint32_t now) {
  return static_cast<int32_t>(now - record.expiresAtMs) >= 0;
}

bool persist() {
  return storageAvailable() &&
         writeStorageFileAtomic(kStorePath, &store, sizeof(store));
}

bool validStoreRecords() {
  for (size_t index = 0; index < store.count; ++index) {
    const MissedConnectionRecord& record = store.records[index];
    if (record.hidden > 1 ||
        !validStoredText(record.title, sizeof(record.title), false) ||
        !validStoredText(record.message, sizeof(record.message))) {
      return false;
    }
  }
  return true;
}

void removeExpired() {
  const uint32_t now = millis();
  size_t destination = 0;
  bool changed = false;
  for (size_t index = 0; index < store.count; ++index) {
    if (expired(store.records[index], now)) {
      changed = true;
      continue;
    }
    if (destination != index) {
      store.records[destination] = store.records[index];
    }
    ++destination;
  }
  store.count = destination;
  if (changed) {
    persist();
  }
}

void sendError(WebServer& server, int status, const __FlashStringHelper* message) {
  String body = F("{\"error\":\"");
  body += message;
  body += F("\"}");
  server.send(status, "application/json; charset=utf-8", body);
}

void handleList(WebServer& server) {
  removeExpired();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json; charset=utf-8", "");
  server.sendContent(F("{\"missed\":["));
  String response;
  response.reserve(AppConfig::kMissedMessageMaxBytes + 200);
  bool first = true;
  const uint32_t now = millis();
  for (size_t index = 0; index < store.count; ++index) {
    const MissedConnectionRecord& record = store.records[index];
    if (record.hidden) {
      continue;
    }
    response = "";
    if (!first) {
      response += ',';
    }
    first = false;
    response += F("{\"id\":");
    response += record.id;
    response += F(",\"title\":\"");
    response += escapeJson(record.title);
    response += F("\",\"message\":\"");
    response += escapeJson(record.message);
    response += F("\",\"createdUptimeSeconds\":");
    response += record.createdAtMs / 1000UL;
    response += F(",\"expiresInSeconds\":");
    response += (record.expiresAtMs - now) / 1000UL;
    response += '}';
    server.sendContent(response);
  }
  response = F("],\"totalSubmitted\":");
  response += store.totalSubmitted;
  response += '}';
  server.sendContent(response);
}

void handleCreate(WebServer& server) {
  removeExpired();
  if (!formRequestWithinLimits(server)) {
    sendError(server, 413, F("Request is too large"));
    return;
  }
  if (!loaded || !storageAvailable()) {
    sendError(server, 503, F("Persistent storage is unavailable"));
    return;
  }

  String title = server.arg("title");
  String message = server.arg("message");
  title.trim();
  message.trim();
  if (title.isEmpty() || message.isEmpty()) {
    sendError(server, 400, F("Title and message are required"));
    return;
  }
  if (!validUserText(title, false) || !validUserText(message)) {
    sendError(server, 400, F("Missed connection contains invalid text"));
    return;
  }
  if (title.length() > AppConfig::kMissedTitleMaxBytes ||
      message.length() > AppConfig::kMissedMessageMaxBytes) {
    sendError(server, 413, F("Missed connection is too long"));
    return;
  }
  uint32_t submissionHash = appendSubmissionHash(0, title);
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
  MissedConnectionRecord prunedRecord{};
  if (store.count >= AppConfig::kMaxMissedConnections) {
    prunedRecord = store.records[0];
    for (size_t index = 1; index < store.count; ++index) {
      store.records[index - 1] = store.records[index];
    }
    --store.count;
    pruned = true;
  }

  MissedConnectionRecord& record = store.records[store.count];
  memset(&record, 0, sizeof(record));
  record.id = store.nextId++;
  record.createdAtMs = millis();
  record.expiresAtMs = record.createdAtMs + AppConfig::kPublicPostLifetimeMs;
  record.bootId = persistedBootCount();
  strlcpy(record.title, title.c_str(), sizeof(record.title));
  strlcpy(record.message, message.c_str(), sizeof(record.message));
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
    sendError(server, 507, F("Could not save missed connection"));
    return;
  }

  lastSubmissionHash = submissionHash;
  lastSubmissionAtMs = millis();
  lastSubmissionId = record.id;

  String response = F("{\"id\":");
  response += record.id;
  response += '}';
  server.send(201, "application/json; charset=utf-8", response);
}

}  // namespace

bool startMissedConnections() {
  store = {};
  const bool valid = readStorageFile(kStorePath, &store, sizeof(store)) &&
                     store.magic == kStoreMagic &&
                     store.version == kStoreVersion &&
                     store.count <= AppConfig::kMaxMissedConnections &&
                     validStoreRecords();
  if (!valid) {
    store = {};
    store.magic = kStoreMagic;
    store.version = kStoreVersion;
    store.nextId = 1;
  }

  const uint32_t now = millis();
  bool changed = false;
  for (size_t index = 0; index < store.count; ++index) {
    MissedConnectionRecord& record = store.records[index];
    if (record.bootId != persistedBootCount()) {
      record.bootId = persistedBootCount();
      record.createdAtMs = now;
      record.expiresAtMs = now + AppConfig::kPublicPostLifetimeMs;
      changed = true;
    }
  }
  loaded = storageAvailable();
  if (changed && !persist()) {
    Serial.println("Failed to refresh missed-connection expiry after reboot.");
  }
  Serial.printf("Missed Connections ready: %u active, %lu submitted.\n",
                static_cast<unsigned>(store.count),
                static_cast<unsigned long>(store.totalSubmitted));
  return loaded;
}

void registerMissedConnectionRoutes(WebServer& server) {
  server.on("/missed", HTTP_GET, [&server]() {
    server.send_P(200, "text/html; charset=utf-8", kMissedPage);
  });
  server.on("/api/missed", HTTP_GET, [&server]() { handleList(server); });
  server.on("/api/missed", HTTP_POST, [&server]() { handleCreate(server); });
}

size_t activeMissedConnectionCount() {
  removeExpired();
  size_t count = 0;
  for (size_t index = 0; index < store.count; ++index) {
    if (!store.records[index].hidden) {
      ++count;
    }
  }
  return count;
}

uint32_t totalMissedConnectionsSubmitted() {
  return store.totalSubmitted;
}

size_t storedMissedConnectionCount() {
  removeExpired();
  return store.count;
}

const MissedConnectionRecord* missedConnectionAt(size_t index) {
  removeExpired();
  return index < store.count ? &store.records[index] : nullptr;
}

bool setMissedConnectionHidden(uint32_t id, bool hidden) {
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

bool deleteMissedConnection(uint32_t id) {
  for (size_t index = 0; index < store.count; ++index) {
    if (store.records[index].id == id) {
      const MissedConnectionRecord removed = store.records[index];
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
