#include "admin.h"

#include "app_config.h"
#include "diagnostics.h"
#include "letters.h"
#include "missed_connections.h"
#include "notices.h"
#include "web_utils.h"

namespace {

constexpr char kAdminPage[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Postie | Strawberry Post</title><style>
body{margin:0;background:#f1dfbd;color:#44211b;font:16px system-ui,sans-serif}main{max-width:52rem;margin:auto;padding:1rem}h1,h2{color:#9f202a}
.card{background:#fffaf0;padding:1rem;margin:1rem 0;border:1px solid #c99b78;border-radius:.4rem}button{padding:.55rem;margin:.2rem;border:0;border-radius:.3rem;background:#9f202a;color:#fff;font:inherit}
button.alt{background:#65544a}dt{font-weight:700;margin-top:.5rem}dd{margin-left:0;white-space:pre-wrap}.private{border-left:5px solid #9f202a;padding-left:.8rem}
</style></head><body><main><h1>Postie&rsquo;s Sorting Room</h1><p>This page and its APIs require Postie authentication.</p>
<p><a href="/postie/diagnostics">System diagnostics</a></p><h2>Letters</h2><section id="letters"></section><h2>Notice moderation</h2><section id="notices"></section><h2>Missed Connections moderation</h2><section id="missed"></section><p id="result" role="status"></p><script>
const result=document.querySelector('#result');function el(tag,text,cls){const n=document.createElement(tag);if(text!==undefined)n.textContent=text;if(cls)n.className=cls;return n}
async function post(url,data){const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data)});const body=await r.json();result.textContent=body.error||'Saved.';if(r.ok)load()}
function button(label,fn,cls){const b=el('button',label,cls);b.onclick=fn;return b}
async function load(){const r=await fetch('/api/admin/overview');if(!r.ok){result.textContent='Authentication failed.';return}const d=await r.json();
const letters=document.querySelector('#letters');letters.replaceChildren(...d.letters.map(x=>{const c=el('details',undefined,'card');if(x.status==='Waiting')c.open=true;const s=el('summary',`${x.tracking} — ${x.recipient} — ${x.status}`);const dl=el('dl',undefined,'private');[['Likely location',x.location],['Message',x.message],['Sender',x.sender||'Anonymous']].forEach(([a,b])=>{dl.append(el('dt',a),el('dd',b))});c.append(s,dl);['Written','OutForDelivery','Delivered','CouldNotFind'].forEach(status=>c.append(button(status,()=>post('/api/admin/letters/status',{id:x.id,status}))));return c}));
const notices=document.querySelector('#notices');notices.replaceChildren(...d.notices.map(x=>{const c=el('article',undefined,'card');c.append(el('strong',x.category),el('p',x.message),button(x.hidden?'Unhide':'Hide',()=>post('/api/admin/notices/moderate',{id:x.id,action:x.hidden?'unhide':'hide'}),'alt'),button('Delete',()=>post('/api/admin/notices/moderate',{id:x.id,action:'delete'})));return c}));
const missed=document.querySelector('#missed');missed.replaceChildren(...d.missed.map(x=>{const c=el('article',undefined,'card');c.append(el('strong',x.title),el('p',x.message),button(x.hidden?'Unhide':'Hide',()=>post('/api/admin/missed/moderate',{id:x.id,action:x.hidden?'unhide':'hide'}),'alt'),button('Delete',()=>post('/api/admin/missed/moderate',{id:x.id,action:'delete'})));return c}))}load();
</script></main></body></html>
)HTML";

constexpr char kDiagnosticsPage[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Diagnostics | Strawberry Post</title>
<style>body{margin:0;background:#f1dfbd;color:#44211b;font:16px system-ui}main{max-width:42rem;margin:auto;padding:1rem}h1{color:#9f202a}dl{display:grid;grid-template-columns:1fr 1fr;background:#fffaf0;padding:1rem}dt{font-weight:700}dd{margin:0;text-align:right}</style></head>
<body><main><p><a href="/postie">&larr; Sorting Room</a></p><h1>System diagnostics</h1><dl id="data"></dl><p>Refresh this page while testing with multiple phones.</p><script>
fetch('/api/admin/diagnostics').then(r=>r.json()).then(data=>{const dl=document.querySelector('#data');Object.entries(data).forEach(([key,value])=>{const dt=document.createElement('dt'),dd=document.createElement('dd');dt.textContent=key;dd.textContent=value;dl.append(dt,dd)})});
</script></main></body></html>
)HTML";

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
  server.sendContent(F("],\"missed\":["));
  for (size_t index = 0; index < storedMissedConnectionCount(); ++index) {
    const MissedConnectionRecord* record = missedConnectionAt(index);
    response = "";
    if (index > 0) response += ',';
    response += F("{\"id\":");
    response += record->id;
    response += ',';
    appendJsonField(response, "title", record->title);
    response += ',';
    appendJsonField(response, "message", record->message);
    response += F(",\"hidden\":");
    response += record->hidden ? F("true}") : F("false}");
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

void handleMissedModeration(WebServer& server) {
  recordHttpRequest(server);
  if (!authenticate(server)) return;
  if (!formRequestWithinLimits(server)) {
    sendError(server, 413, F("Request is too large"));
    return;
  }
  const uint32_t id = server.arg("id").toInt();
  const String action = server.arg("action");
  bool success = false;
  if (action == "hide") success = setMissedConnectionHidden(id, true);
  else if (action == "unhide") success = setMissedConnectionHidden(id, false);
  else if (action == "delete") success = deleteMissedConnection(id);
  else {
    sendError(server, 400, F("Invalid moderation action"));
    return;
  }
  if (!success) {
    sendError(server, 404, F("Missed connection not found or could not be saved"));
    return;
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

}  // namespace

void registerAdminRoutes(WebServer& server) {
  server.on("/postie", HTTP_GET, [&server]() {
    recordHttpRequest(server);
    if (authenticate(server)) {
      server.send_P(200, "text/html; charset=utf-8", kAdminPage);
    }
  });
  server.on("/postie/diagnostics", HTTP_GET, [&server]() {
    recordHttpRequest(server);
    if (authenticate(server)) {
      server.send_P(200, "text/html; charset=utf-8", kDiagnosticsPage);
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
  server.on("/api/admin/notices/moderate", HTTP_POST,
            [&server]() { handleNoticeModeration(server); });
  server.on("/api/admin/missed/moderate", HTTP_POST,
            [&server]() { handleMissedModeration(server); });
}
