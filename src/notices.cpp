#include "notices.h"

#include <string.h>

#include "storage.h"
#include "web_utils.h"

namespace {

constexpr char kNoticesPath[] = "/notices.dat";
constexpr uint32_t kNoticesMagic = 0x4e4f5443;  // "NOTC"
constexpr uint16_t kNoticesVersion = 2;

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

constexpr char kNoticePage[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Notice Board | Strawberry Post</title><link rel="stylesheet" href="/style.css"></head><body><main><a class="back" href="/">&larr; Strawberry Post</a><h1>Notice Board</h1><p class="hint">Notices stay up for about eight hours.</p>
<form id="form"><label>Category<input name="category" maxlength="32" required></label>
<label>Message<textarea name="message" maxlength="280" required></textarea></label><button>Pin notice</button></form>
<p id="result" role="status"></p><section id="posts"></section><script>
const form=document.querySelector('#form'),result=document.querySelector('#result'),posts=document.querySelector('#posts');
async function load(){const r=await fetch('/api/notices');const data=await r.json();posts.replaceChildren(...data.notices.map(n=>{const article=document.createElement('article');article.className='post';const strong=document.createElement('strong');strong.textContent=n.category;const p=document.createElement('p');p.textContent=n.message;article.append(strong,p);return article}))}
form.addEventListener('submit',async e=>{e.preventDefault();result.textContent='Posting...';const r=await fetch('/api/notices',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(new FormData(form))});const data=await r.json();result.textContent=data.error||'Notice pinned.';if(r.ok){form.reset();load()}});load();
</script></main></body></html>
)HTML";

bool expired(const NoticeRecord& notice, uint32_t now) {
  return static_cast<int32_t>(now - notice.expiresAtMs) >= 0;
}

bool persist() {
  return storageAvailable() &&
         writeStorageFileAtomic(kNoticesPath, &store, sizeof(store));
}

bool validStoreRecords() {
  for (size_t index = 0; index < store.count; ++index) {
    const NoticeRecord& notice = store.records[index];
    if (notice.hidden > 1 ||
        !validStoredText(notice.category, sizeof(notice.category), false) ||
        !validStoredText(notice.message, sizeof(notice.message))) {
      return false;
    }
  }
  return true;
}

bool removeExpired() {
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
  return changed;
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
  server.sendContent(F("{\"notices\":["));
  String response;
  response.reserve(AppConfig::kNoticeMessageMaxBytes + 160);
  bool first = true;
  const uint32_t now = millis();
  for (size_t index = 0; index < store.count; ++index) {
    const NoticeRecord& notice = store.records[index];
    if (notice.hidden) {
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
  const bool valid = readStorageFile(kNoticesPath, &store, sizeof(store)) &&
                     store.magic == kNoticesMagic &&
                     store.version == kNoticesVersion &&
                     store.count <= AppConfig::kMaxNotices && validStoreRecords();
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
  server.on("/notices", HTTP_GET,
            [&server]() { server.send_P(200, "text/html; charset=utf-8", kNoticePage); });
  server.on("/api/notices", HTTP_GET, [&server]() { handleList(server); });
  server.on("/api/notices", HTTP_POST, [&server]() { handleCreate(server); });
}

size_t activeNoticeCount() {
  removeExpired();
  size_t count = 0;
  for (size_t index = 0; index < store.count; ++index) {
    if (!store.records[index].hidden) {
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
  return store.count;
}

const NoticeRecord* noticeAt(size_t index) {
  removeExpired();
  return index < store.count ? &store.records[index] : nullptr;
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
