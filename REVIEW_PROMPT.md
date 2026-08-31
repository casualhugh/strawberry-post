# Strawberry Post advanced review prompt

Copy everything below this line into the reviewing agent.

---

You are a senior embedded-systems, reliability, privacy, and web-security code
reviewer. Perform a read-only review of this existing PlatformIO project:

`C:\Users\Hughe\Documents\projects\mail-notice-board`

Do not edit files, create patches, flash hardware, or add dependencies. Inspect
the current source, `README.md`, `platformio.ini`, and Git history. Build from
the repository root with exactly:

```powershell
C:\Users\Hughe\.platformio\penv\Scripts\platformio.exe run --environment esp32s3
```

Report build success/failure, warnings, and RAM/flash usage. A build proves only
compilation; do not claim physical, captive-network, power-loss, SD-card, or
multi-phone validation unless such evidence exists.

Also inspect both Unity suites under `test/native/` and, with a host GCC/G++
compiler available on `PATH`, run:

```powershell
C:\Users\Hughe\.platformio\penv\Scripts\platformio.exe test --environment native
```

The latest local run used MSYS2 UCRT64 GCC 16.1.0 and passed all 23 test
functions. Reproduce that result independently; do not rely on this statement
as proof of the current checkout.

## Design specification

Strawberry Post is a fully offline festival web application hosted entirely by
an ESP32-S3. It creates an open local Wi-Fi AP; phones connect directly. There
is no internet, cloud account, upstream router, external asset, or CDN.

Public features:

1. Notice Board
2. Missed Connections
3. Digital Letters

An unlinked, authenticated Postie interface reads private letters, changes
their status, and moderates public content.

Use an embedded-suitable architecture: Arduino, `WiFi.h`, a lightweight HTTP
server, `DNSServer`, LittleFS for persistent internal data, bounded structures,
defensive parsing, small local assets, minimal allocation, and understandable
modules. Do not introduce React, Node/npm on the device, external services,
large web frameworks, SQLite, desktop server concepts, or display code.

Network goals:

- SSID similar to `STRAWBERRY POST no internet`
- deterministic private address
- `http://post.local/` where practical through wildcard DNS
- direct-IP fallback
- captive-network probes as convenience only
- deliberately local/short-range installation, with radio tuning deferred
- no user accounts

UI goals: mobile-first, readable outdoors, lightweight, and styled as a small
Australian rural postal service using red, cream, paper, and cork tones.

Notice records require stable IDs, category, message, creation/expiry data, and
moderation state. Missed Connections require ID, title/“to”, message,
creation/expiry, and moderation state. They default to about eight hours. No
NTP/RTC can be assumed, so uptime expiry is acceptable if its limitations are
explicit and future time improvement remains possible.

Digital Letters require ID, human-friendly tracking code, recipient, likely
location, private message, optional sender, creation data, and one status:

- `Waiting`
- `Written`
- `OutForDelivery`
- `Delivered`
- `CouldNotFind`

Letter contents must never appear publicly. The sender receives a tracking code
after submission. Public tracking may return only tracking code and status.

Statistics should be derived automatically: submitted/waiting/delivered
letters, active/total notices, and active/total missed connections.

The trust model is an informal offline festival installation, not a hardened
internet service. Nevertheless, authenticate every admin route, centralize
credentials, protect private content, bound inputs and records, handle malformed
and oversized requests safely, encode user content correctly, tolerate repeated
taps, and support valid emoji. Expect nonsense, empty values, very long values,
malformed bytes, and users trying unintended inputs.

Current web assets intentionally live in `PROGMEM` so the site shell survives a
data-filesystem failure. A proposed future option is a laptop-editable FAT32 SD
web bundle loaded and validated at boot into PSRAM, with the embedded bundle as
fallback. That SD feature is not implemented and must not be confused with
LittleFS, which remains the internal persistent-data store.

## Stage contract

Stages 0–10 are implemented and under review. Stage 11 is forbidden unless
separately authorized.

- **Stage 0 — Baseline:** inspect `platformio.ini`, identify environment/board/
  framework, and prove the untouched project builds from the CLI.
- **Stage 1 — AP/minimal HTTP:** AP startup, deterministic IP, `GET /`, minimal
  running page, and serial startup/IP state. No storage or forms.
- **Stage 2 — DNS/captive groundwork:** wildcard DNS, useful mobile/desktop
  connectivity probes, and direct homepage access without relying on a popup.
- **Stage 3 — Persistence foundation:** LittleFS startup, safe read/write
  helpers, and a reboot-surviving persistence proof.
- **Stage 4 — Notice Board:** persistent `GET/POST /api/notices`, required
  category/message, validation, capacity, expiry, and basic UI.
- **Stage 5 — Missed Connections:** persistent `GET/POST /api/missed`, expiry,
  validation, capacity, and basic UI.
- **Stage 6 — Digital Letters:** persistent `POST /api/letters`, unique friendly
  tracking codes, and public status-only lookup.
- **Stage 7 — Postie/Admin:** authenticated private letter workflow, all status
  changes, and hide/unhide/delete moderation. No public admin link.
- **Stage 8 — Public UI:** proper mobile pages, local assets only, and derived
  postal statistics.
- **Stage 9 — Reliability:** input and record limits, empty/malformed handling,
  encoding, pruning, duplicate taps, full/corrupt storage behavior, watchdog/
  heap/stack friendliness, and useful diagnostics.
- **Stage 10 — Load-test preparation:** AP-client, request, heap, storage, and
  record diagnostics; prepare for rather than assume multi-phone success.
- **Stage 11 — Display:** not begun. There must be no e-ink/display dependency,
  code, pin configuration, layout, refresh, or power behavior.

Every stage was required to build before the next and remain a separate commit.
Note that `532fa0c` is an additional Stage 1 design-decision commit, not a stage
number. A later, post-stage refactor extracted `lib/strawberry_core/` and added
native tests. Determine actual compliance from code and history, not commit
titles.

## Known decisions to assess rather than silently reinterpret

- AP is open; HTTP and Basic Auth are plaintext.
- The checked-in admin password is a deployment placeholder and must change.
- Eight clients is an association cap, not a measured throughput target or
  range control.
- Arduino `WebServer` is synchronous by design; require evidence before asking
  for an async rewrite.
- HTML/CSS/JS are in `PROGMEM` for reliability.
- Public expiry uses `millis()`, and current reboot behavior grants a new full
  eight-hour window.
- Persistent data is stored as bounded native binary snapshots with
  application magic/version fields and `.tmp`/`.bak` replacement.
- `STPS`, `NOTC`, `MISS`, and `LETR` are application file signatures, not
  LittleFS requirements.
- LittleFS auto-format is restricted to storage that appears blank.
- Duplicate suppression remembers one recent payload hash per feature for five
  seconds; it is not rate limiting.
- Notice/Missed capacity prunes oldest. Letter capacity prunes a completed or
  failed record; all-active capacity rejects new letters.
- Four-digit tracking codes are friendly identifiers, not secrets.
- Hardware, captive behavior, power-loss recovery, endurance, and multi-phone
  operation are not physically proven.

## Required review areas

### Dead code, readability, and maintainability

- Identify unused functions, fields, includes, constants, routes, JavaScript,
  assets, and unreachable branches. Distinguish intentionally reserved schema
  space from accidental dead state.
- Check whether comments explain *why* rather than narrate syntax.
- Flag unexplained numeric constants. Do not call protocol constants (ports,
  UTF-8 boundaries, FNV constants, erased-flash byte) arbitrary without first
  checking their standard meaning.
- Assess module boundaries, duplicate Notice/Missed logic, giant embedded
  strings, naming, error semantics, API discoverability, and whether a human can
  safely change one feature without violating another.
- Review `README.md` for accuracy against code and identify stale or misleading
  documentation.

### Persistence and power loss

- Walk every failure/power-cut point in `writeStorageFileAtomic`: temporary
  creation, write, flush/close, backup removal, both renames, restoration, and
  cleanup. Consider ignored errors and orphaned files.
- Review validated backup fallback, reported load source, rejected-primary
  removal, backup-only recovery, and failure to prepare that recovery.
- Inspect expiry compaction and every mutation rollback; determine whether a
  failed persist can resurrect content on reboot.
- Evaluate blank-partition detection, its 64-byte probe, hard-coded partition
  label/subtype, and the risk of formatting damaged or partially erased data.
- Assess whole-store rewrite latency, write amplification, and flash endurance.
- Determine behavior when storage is missing, full, corrupt, or incompatible.

### Binary formats and integrity

- Review struct padding, alignment, endianness, enum representation, ABI/
  toolchain changes, size-dependent schemas, and version handling.
- Check magic, version, exact length, semantic validation, missing checksums/
  generations/migrations, duplicate IDs/codes, and counter wrap.
- Assess silent empty-store initialization after invalid data.
- Note that `platform = espressif32` is not pinned; judge reproducibility risk.
- Account for global store BSS plus Wi-Fi/WebServer/LittleFS runtime memory.

### Time and expiry

- Prove or disprove the half-range `millis()` deadline comparison's wrap safety.
- Assess reboot extension, repeated power cycles, misleading creation values,
  cleanup timing, and persistence failure during cleanup.
- Review all boot, ID, request, total, and tracking counter wrap behavior.

### Privacy, auth, and retention

- Prove all private letter fields are reachable only through authenticated
  routes and never leak in public JSON, errors, cache, logs, or redirects.
- Assess open AP, plaintext Basic Auth, default password, browser caching,
  brute force, CSRF, missing logout, and deployment guard against the stated
  offline trust model.
- Review plaintext-at-rest letters, no explicit letter deletion/expiry, capacity
  eviction, and operator retention needs.
- Evaluate the 10,000-code namespace, enumeration, reuse after pruning, and old
  codes later referring to new letters.
- Assess permitted status transitions and moderation error semantics.

### HTTP, DNS, captive portals, and request limits

- Verify every route and wrong-method behavior, captive probes, unknown-path
  redirects, direct-IP fallback, DNS failure, host headers, and API error types.
- `post.local` uses `.local`, commonly mDNS-special, but the firmware implements
  wildcard unicast DNS and no mDNS responder. Require real-device evidence.
- Verify chunked JSON behavior against the installed Arduino core rather than
  assuming a missing final chunk.
- Audit malformed/overflowing `Content-Length`, chunked requests, unexpected
  content types, duplicate fields, many arguments, huge URI/query, numeric
  `toInt()` permissiveness, and slow clients.
- Determine whether handler-level 4 KiB checks happen too late: synchronous
  `WebServer` may allocate/parse the complete body before handler dispatch.

### UTF-8, emoji, encoding, and injection

- Test ASCII controls, invalid continuations, overlong encodings, truncation,
  surrogates, values above U+10FFFF, four-byte emoji, combining text, NUL, and
  percent-decoding behavior.
- Verify byte limits, `strlcpy`, and browser character/code-unit `maxlength`
  differences never split accepted UTF-8.
- Confirm whitespace-only behavior, JSON escaping, DOM `textContent`, and every
  rendering path. Do not demand HTML escaping for values never interpolated as
  HTML.

### Duplicate handling, capacity, and abuse

- Test repeated taps, timeout, reboot, alternating payloads, global identical
  submissions from different phones, 32-bit hash collision, and stored-result
  correctness after deletion/pruning/status changes.
- Assess unauthenticated record flooding: public eviction and 32 active letters
  denying subsequent senders.
- Check tracking uniqueness only among retained records and later reuse.

### Heap, stack, blocking, and multi-phone behavior

- Account for BSS, loop-task stack, local rollback copies, HTTP body parsing,
  `String` growth/escaping, JSON chunks, and PSRAM assumptions.
- Verify PROGMEM assets are served without unnecessary RAM copies.
- Analyze synchronous HTTP, DNS processing, serial output, full-store flash
  writes, slow clients, `delay(2)`, and watchdog/service starvation.
- Do not equate eight associations with throughput. Define measurable latency,
  error, reset, heap, and persistence criteria for 1/2/4/8 phones.

### Diagnostics, testing, SD proposal, and operability

- Review diagnostics authentication, privacy, cost, accuracy, and missing
  reset reason, stack high-water, latency/status, rejected/duplicate,
  persistence-failure, and DNS-health signals.
- Note that the diagnostics request records itself before reporting `lastUri`.
- Review whether the 23 native test functions exercise the production pure-core
  implementations and whether the fake clock, memory file store, corruption,
  and failure injection model the claimed branches. Do not report them as
  executed unless a native runner actually completed them.
- Confirm there is still no hardware, captive, real LittleFS power-cut, soak,
  or multi-phone test evidence.
- Evaluate—not implement—the SD asset proposal. LittleFS should remain internal
  data storage; laptop-editable SD should use FAT/FAT32 through `SD`/`SD_MMC`.
  Assess boot-only read, PSRAM cache, manifest/hash/size validation, read-only
  operation, embedded fallback, all-or-nothing bundle selection, missing card,
  bad FAT, hot removal, exact card hardware, and SPI pin selection.
- Confirm display work is entirely absent. Do not recommend adding it.

## Required response format

Lead with actionable findings ordered by severity:

- `[P0]` catastrophic data loss, private-data exposure, or reliable crash
- `[P1]` release-blocking correctness, resilience, privacy, or availability
- `[P2]` significant defect or operational risk
- `[P3]` lower-impact hardening, maintainability, test, or diagnostic issue

For each finding:

1. concise title;
2. exact file and line reference;
3. concrete trigger/failure scenario;
4. user/data/privacy/availability impact;
5. relevant requirement/stage;
6. proportionate remediation or uncertainty-resolving test;
7. explicit label when hardware/framework dependent.

Do not manufacture a finding for every checklist item. Exclude vague, generic,
style-only, or preference-only feedback. Give credit for controls implemented
correctly. Do not excuse demonstrated defects merely because the network is
offline.

After findings, provide:

- build and repository verification;
- Stage 0–11 compliance matrix using `Compliant`, `Partially compliant`,
  `Non-compliant`, or `Forbidden stage correctly absent`;
- evidence-backed strengths;
- prioritized pre-hardware test plan;
- prioritized real-device plan for power cuts, corruption, emoji boundaries,
  malformed requests, captive behavior, and 1/2/4/8-phone load;
- remaining assumptions and questions.

Do not modify the repository or provide a patch.
