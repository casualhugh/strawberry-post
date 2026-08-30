#include "public_ui.h"

#include "letters.h"
#include "missed_connections.h"
#include "notices.h"

namespace {

// Public UI assets intentionally live in program flash, keeping the complete
// site in one firmware image and independent of LittleFS health. If these grow,
// keep separate HTML/CSS source files and embed them into PROGMEM at build time.
constexpr char kPublicStyles[] PROGMEM = R"CSS(
:root{color-scheme:light;--red:#a92330;--deep:#55251f;--cream:#fff8e7;--paper:#fffdf5;--cork:#c79568;--muted:#78645b}
*{box-sizing:border-box}body{margin:0;background:var(--cream);color:var(--deep);font:17px/1.45 system-ui,-apple-system,sans-serif}
body:before{content:"";display:block;height:.55rem;background:repeating-linear-gradient(135deg,var(--red) 0 18px,var(--paper) 18px 36px,#315b6e 36px 54px,var(--paper) 54px 72px)}
main{width:min(100% - 2rem,42rem);margin:0 auto;padding:1.4rem 0 3rem}h1,h2{color:var(--red);line-height:1.1}h1{font-family:Georgia,serif;font-size:clamp(2.2rem,10vw,3.7rem);letter-spacing:-.03em;margin:.8rem 0}.eyebrow{text-transform:uppercase;letter-spacing:.16em;font-size:.75rem;font-weight:800;color:var(--muted)}
a{color:var(--red)}.menu{display:grid;gap:.8rem;margin:1.5rem 0}.menu a,button{display:block;width:100%;border:0;border-radius:.5rem;background:var(--red);color:#fff;padding:1rem;text-decoration:none;text-align:center;font:700 1.05rem system-ui;box-shadow:0 3px 0 #71141d}.menu a:active,button:active{transform:translateY(2px);box-shadow:0 1px 0 #71141d}
.status,.ticket{background:var(--paper);border:2px dashed var(--cork);padding:1rem;text-align:center}.stats{display:grid;grid-template-columns:repeat(2,1fr);gap:.6rem;margin-top:1rem}.stat{background:var(--paper);padding:.8rem;text-align:center;border:1px solid #ead7bb}.stat strong{display:block;font-size:1.5rem;color:var(--red)}
label{display:block;margin:1rem 0;font-weight:700}input,textarea{display:block;width:100%;margin-top:.35rem;padding:.85rem;border:1px solid #b99b80;border-radius:.35rem;background:#fff;font:inherit;color:inherit}textarea{min-height:9rem;resize:vertical}.post,.card{background:var(--paper);padding:1rem;margin:1rem 0;border-left:5px solid var(--red);box-shadow:0 2px 8px #6b3a2520}.post p{white-space:pre-wrap}.back{display:inline-block;margin:.4rem 0 1rem}.hint{color:var(--muted);font-size:.92rem}[hidden]{display:none!important}
@media(min-width:38rem){.menu{grid-template-columns:repeat(3,1fr)}.stats{grid-template-columns:repeat(4,1fr)}}
)CSS";

void sendStats(WebServer& server) {
  String response = F("{\"lettersSubmitted\":");
  response += totalLettersSubmitted();
  response += F(",\"lettersWaiting\":");
  response += lettersWithStatus(LetterStatus::Waiting);
  response += F(",\"lettersOutForDelivery\":");
  response += lettersWithStatus(LetterStatus::OutForDelivery);
  response += F(",\"lettersDelivered\":");
  response += lettersWithStatus(LetterStatus::Delivered);
  response += F(",\"noticesActive\":");
  response += activeNoticeCount();
  response += F(",\"noticesSubmitted\":");
  response += totalNoticesSubmitted();
  response += F(",\"missedActive\":");
  response += activeMissedConnectionCount();
  response += F(",\"missedSubmitted\":");
  response += totalMissedConnectionsSubmitted();
  response += '}';
  server.send(200, "application/json; charset=utf-8", response);
}

}  // namespace

void sendPublicHome(WebServer& server) {
  const size_t outForDelivery =
      lettersWithStatus(LetterStatus::OutForDelivery);
  String page = F("<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>Strawberry Post</title><link rel=\"stylesheet\" href=\"/style.css\"></head><body><main><p class=\"eyebrow\">A tiny rural postal service</p><h1>Strawberry Post</h1><p>Letters, notices and hopeful messages, delivered locally with no internet required.</p><nav class=\"menu\"><a href=\"/letters\">Send a Letter</a><a href=\"/notices\">Notice Board</a><a href=\"/missed\">Missed Connections</a></nav><p class=\"status\">");
  page += outForDelivery;
  page += outForDelivery == 1
              ? F(" letter currently out for delivery")
              : F(" letters currently out for delivery");
  page += F("</p><section class=\"stats\"><div class=\"stat\"><strong>");
  page += lettersWithStatus(LetterStatus::Waiting);
  page += F("</strong>waiting letters</div><div class=\"stat\"><strong>");
  page += lettersWithStatus(LetterStatus::Delivered);
  page += F("</strong>delivered</div><div class=\"stat\"><strong>");
  page += activeNoticeCount();
  page += F("</strong>active notices</div><div class=\"stat\"><strong>");
  page += activeMissedConnectionCount();
  page += F("</strong>missed connections</div></section><p class=\"hint\">Connected to STRAWBERRY POST — no internet needed.</p></main></body></html>");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html; charset=utf-8", page);
}

void registerPublicUiRoutes(WebServer& server) {
  server.on("/style.css", HTTP_GET, [&server]() {
    server.sendHeader("Cache-Control", "public, max-age=3600");
    server.send_P(200, "text/css; charset=utf-8", kPublicStyles);
  });
  server.on("/api/stats", HTTP_GET, [&server]() { sendStats(server); });
}
