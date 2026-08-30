# Strawberry Post

Strawberry Post is a completely offline festival web application hosted by an
ESP32-S3. The device creates its own Wi-Fi network. Attendees connect directly
with their phones and use a small local website; no internet service, cloud
account, or upstream router is required.

The project currently implements Stages 0–10 below. Display hardware is
explicitly deferred and must not be added without a separate decision.

## Product specification

The public application has three services:

1. **Notice Board** — short-lived categorized public notices.
2. **Missed Connections** — short-lived public messages to people encountered
   around the festival.
3. **Digital Letters** — private messages submitted to the Postie and tracked
   publicly by status only.

The unlinked Postie interface allows an authenticated operator to read private
letters, update delivery status, and moderate public posts.

The visual language is a small fictional Australian rural post office: red,
cream, paper, and cork tones; large mobile controls; no external fonts, CDNs,
or JavaScript frameworks. All current web assets are compiled into program
flash (`PROGMEM`) so the public shell remains available if data storage fails.

### Network behavior

- AP SSID: `STRAWBERRY POST no internet`
- AP address and gateway: `192.168.4.1/24`
- HTTP: port 80
- Wildcard DNS: port 53, resolving names to the ESP32
- Friendly URL: `http://post.local/`
- Direct fallback: `http://192.168.4.1/`
- Initial association limit: eight clients

The eight-client value limits Wi-Fi associations; it does not control radio
range and is not proof of eight-phone HTTP throughput. Android, Apple, and
Windows captive-network probe paths are handled as a convenience. The product
must remain usable when a phone does not open a captive window automatically.

### Data models and current limits

All text limits below are UTF-8 byte limits. Browser `maxlength` values count
characters/code units and may therefore reject or accept a different number of
emoji before the server applies its byte limit.

| Record | Fields | Current capacity and limits |
| --- | --- | --- |
| Notice | ID, category, message, creation/expiry uptime, boot ID, hidden flag | 32 records; category 32 bytes; message 512 bytes; about 8 hours |
| Missed connection | ID, title/“to”, message, creation/expiry uptime, boot ID, hidden flag | 32 records; title 96 bytes; message 512 bytes; about 8 hours |
| Digital letter | ID, tracking code, recipient, likely location, private message, optional sender, creation data, status | 32 records; recipient 160 bytes; location 96 bytes; message 768 bytes; sender 96 bytes |

Letter statuses are:

- `Waiting`
- `Written`
- `OutForDelivery`
- `Delivered`
- `CouldNotFind`

Public tracking returns only the tracking code and status. It must never return
recipient, location, message, or sender. Tracking codes currently use the
friendly form `STRAW-0427` and are not intended as cryptographic secrets.

Statistics are derived from stored activity rather than manually edited.

### Time semantics

There is no NTP or guaranteed real-time clock. Public expiry uses `millis()`
and a boot ID. On reboot, currently stored public posts receive a new eight-hour
window because elapsed power-off time is unknowable. Repeated reboots can
therefore extend a post. A future admin-supplied clock or battery-backed RTC is
an architectural option, not yet implemented.

## Architecture

```text
Phones
  |
  +-- ESP32 SoftAP + wildcard DNS
        |
        +-- synchronous HTTP routes
        |     +-- public UI and APIs
        |     +-- authenticated Postie APIs
        |     +-- protected diagnostics
        |
        +-- program flash (PROGMEM)
        |     +-- HTML, CSS, and JavaScript
        |
        +-- internal flash (LittleFS)
              +-- system state and boot count
              +-- notice snapshot
              +-- missed-connection snapshot
              +-- private-letter snapshot
```

Important modules:

- `src/app_config.h` — deployment and resource limits.
- `src/main.cpp` — startup ordering and cooperative main loop.
- `src/wifi_manager.*` — deterministic SoftAP configuration.
- `src/dns_server.*` — wildcard local DNS.
- `src/web_server.*` — server setup, captive routes, and fallback routing.
- `src/public_ui.*` — shared public styling, homepage, and statistics.
- `src/notices.*` — notice model, persistence, routes, and moderation hooks.
- `src/missed_connections.*` — missed-connection equivalent.
- `src/letters.*` — private letters, tracking, status, and admin hooks.
- `src/admin.*` — Basic-authenticated Postie UI and APIs.
- `src/storage.*` — LittleFS mounting and replacement/recovery helpers.
- `src/storage_format.h` — readable application file-signature construction.
- `src/web_utils.*` — JSON escaping, UTF-8 validation, request policy, and
  duplicate-submission hashing.
- `src/diagnostics.*` — request counters and runtime health reporting.

The HTTP server is intentionally synchronous. Several phones can remain
associated with the AP, but HTTP requests are handled one at a time. Flash
writes, slow clients, and large responses can delay other HTTP and DNS work;
this must be characterized on real hardware before considering an async server.

## Persistence and the “magic numbers”

LittleFS is mounted on an internal ESP32 flash partition. LittleFS does **not**
require the application’s `STPS`, `NOTC`, `MISS`, or `LETR` values.

Those four-character tags are Strawberry Post file-format sentinels. Each
binary snapshot contains an application magic value and schema version so the
firmware can reject a wrong file type or an incompatible layout instead of
interpreting arbitrary bytes as records. `makeStorageMagic()` keeps the tag
readable in code instead of duplicating unexplained hexadecimal constants.

Current files are:

- `/system.dat` — `STPS`, schema 1
- `/notices.dat` — `NOTC`, schema 2
- `/missed.dat` — `MISS`, schema 2
- `/letters.dat` — `LETR`, schema 1

Files are native fixed C++ binary snapshots. That is compact and bounded, but
it couples data compatibility to struct layout, capacity, compiler ABI, and
schema version. There is currently no checksum or migration layer. Any future
record-layout change must deliberately bump the version and define whether old
festival data is migrated, exported, or discarded.

Writes use a temporary file and backup rename sequence. If LittleFS fails to
mount and the partition appears non-blank, firmware refuses to format it. An
apparently blank first-use partition may be formatted. The public UI remains in
program flash, but new submissions return a storage-unavailable response.

Notice and missed-connection stores prune their oldest record when full. A full
letter store prunes the oldest completed/failed letter; if all 32 letters are
still active, a new letter is rejected.

## Security and privacy model

This is an offline festival installation, not a high-security service:

- The AP is open and HTTP is plaintext.
- Postie APIs use HTTP Basic authentication.
- The default password in `src/app_config.h` **must be changed before use**.
- Basic auth protects application routes but does not encrypt Wi-Fi traffic.
- Letter bodies are plaintext in internal flash.
- User input is bounded and validated as UTF-8; valid emoji are accepted.
- JSON output is escaped, and browser pages render user data with `textContent`.
- Duplicate-tap handling remembers one recent content hash per feature for five
  seconds. It is a usability guard, not general rate limiting.

The public Postie page is not linked from public navigation. Its location is
not a security boundary; authentication is required independently on every
admin endpoint.

## Public and administrative routes

| Method | Route | Purpose |
| --- | --- | --- |
| GET | `/` | Public homepage and derived summary |
| GET | `/style.css` | Flash-resident public stylesheet |
| GET | `/notices` | Notice Board page |
| GET/POST | `/api/notices` | List/create notices |
| GET | `/missed` | Missed Connections page |
| GET/POST | `/api/missed` | List/create missed connections |
| GET | `/letters` | Letter submission and tracking page |
| POST | `/api/letters` | Create a private letter |
| GET | `/api/letters/status?tracking=...` | Public status-only tracking |
| GET | `/api/stats` | Derived public statistics |
| GET | `/postie` | Authenticated sorting room |
| GET | `/postie/diagnostics` | Authenticated diagnostics page |
| GET | `/api/admin/overview` | Private letters and moderation data |
| POST | `/api/admin/letters/status` | Change letter status |
| POST | `/api/admin/notices/moderate` | Hide/unhide/delete a notice |
| POST | `/api/admin/missed/moderate` | Hide/unhide/delete a missed connection |
| GET | `/api/admin/diagnostics` | Runtime diagnostic JSON |

## Build

PlatformIO environment:

- Platform: `espressif32`
- Board: `adafruit_feather_esp32s3`
- Framework: Arduino
- Environment: `esp32s3`
- Filesystem: LittleFS

PlatformIO is not assumed to be on `PATH` on the original development machine.

```powershell
C:\Users\Hughe\.platformio\penv\Scripts\platformio.exe run --environment esp32s3
```

Upload and monitor once hardware is connected:

```powershell
C:\Users\Hughe\.platformio\penv\Scripts\platformio.exe run --environment esp32s3 --target upload
C:\Users\Hughe\.platformio\penv\Scripts\platformio.exe device monitor --baud 115200
```

The firmware has compiled successfully, but no claim is made that it has been
flashed, power-cycle tested, captive-portal tested, or load tested on hardware.

## Pre-hardware testing plan

Compilation is useful but insufficient. Most domain behavior can be tested on
the development computer after a small separation between Arduino adapters and
pure logic.

Recommended host-test structure:

1. Add a PlatformIO `native` test environment using the bundled Unity test
   framework.
2. Extract platform-neutral functions/classes for expiry, pruning, tracking
   allocation, duplicate decisions, record validation, and storage encoding.
3. Inject a fake monotonic clock instead of calling `millis()` inside domain
   logic.
4. Inject a memory-backed file store so successful writes, short writes,
   corrupt primaries, valid backups, and failures at each replacement step can
   be reproduced deterministically.
5. Keep `WiFi`, `WebServer`, `LittleFS`, and `SD` in thin ESP32-only adapters.

High-value native test cases:

- `millis()` wraparound and expiry just before/at/after the deadline.
- Reboot policy for active public records.
- Full-board pruning and full-active-letter rejection.
- Tracking collision search, wraparound, pruning, and later code reuse.
- Empty, whitespace, boundary-length, emoji-heavy, and malformed UTF-8 input.
- JSON escaping for quotes, slashes, controls, and multibyte text.
- Duplicate tap, alternating payload, timeout, and hash-collision behavior.
- Valid, wrong-version, wrong-magic, truncated, exact-sized-corrupt, and backup
  persistence fixtures.
- RAM rollback after every simulated write failure.
- Statistics after create, moderate, status change, expiry, and pruning.

Additional pre-hardware checks:

- Build with `-Wall -Wextra` and fail CI for warnings originating in `src/`.
- Run `pio check` with an available static analyzer.
- Serve future separated HTML assets from a desktop server for human browser,
  accessibility, viewport, and no-network inspection.
- Validate that HTML refers only to local URLs and that public schemas never
  include private letter field names.
- Add fixture-based size checks so a web bundle or persistent snapshot cannot
  silently exceed its configured budget.

An emulator can help with CPU-only logic, but it should not be treated as proof
of ESP32 SoftAP, DNS, captive WebView, LittleFS power-loss, SD wiring, flash
latency, or multiple-phone behavior. Those remain hardware-in-the-loop tests.

## Removable SD web-asset proposal (not implemented)

LittleFS is designed for a flash partition attached to the ESP32. A removable,
laptop-editable SD card should instead be FAT/FAT32 and mounted with Arduino's
`SD` library over SPI or `SD_MMC` with suitable hardware wiring.

The current Feather target does not provide an onboard microSD slot. It needs a
microSD breakout or FeatherWing. The installed board variant currently defines
`SCK=36`, `MOSI=35`, `MISO=37`, and `SS=42`, but these are not committed as the
hardware design. The selected card board's chip-select pin, voltage regulation,
pull-ups, wiring length, and compatibility must be verified first.

Recommended separation:

```text
PersistentDataStore
  +-- LittleFS on internal flash
      (private letters, posts, counters, configuration)

WebAssetProvider
  +-- validated FAT32 SD bundle loaded at boot when available
  +-- embedded PROGMEM bundle as mandatory fallback
```

Do not move private records to the removable card merely to support editable
HTML. Keeping data and presentation on different storage reduces accidental
disclosure and ensures a missing/bad card does not destroy festival records.

Suggested boot-only asset flow:

1. Start with the embedded fallback UI available.
2. Mount the SD card read-only; never auto-format a removable card.
3. Read a small manifest containing bundle version, required filenames, sizes,
   and preferably SHA-256 hashes.
4. Reject path traversal, missing files, unknown versions, or any per-file and
   total-size limit violation.
5. Load the validated bundle into PSRAM when available. Use ordinary heap only
   under a conservative total cap.
6. Serve cached buffers with fixed content types and lengths.
7. Report source (`SD` or `embedded`), bundle version/hash, bytes cached, and
   load errors in protected diagnostics.
8. If any required asset fails, discard the entire SD bundle and use the
   internally consistent embedded bundle rather than mixing versions.

Reading once at setup makes request service fast and permits the card contents
to be replaced from a laptop between boots. The safe operator procedure is:
power off, remove card, edit/replace the complete bundle, eject it cleanly from
the laptop, reinstall it, and boot. Hot removal is not required and should not
be advertised.

Directly streaming large files from SD would save RAM but makes every request
depend on card latency and presence. For this small UI, a bounded PSRAM cache
with an embedded fallback is the preferred design. The fallback also provides
a recovery page when a card is missing or invalid.

## Development stages

Every completed stage must compile before the next begins and should remain a
separate reviewable commit.

| Stage | Requirement | Current state |
| --- | --- | --- |
| 0 | Inspect PlatformIO configuration and prove baseline CLI build | Complete |
| 1 | AP, deterministic IP, minimal `GET /`, serial startup state | Complete |
| 2 | Wildcard DNS and captive-network groundwork | Complete |
| 3 | LittleFS foundation and persistence proof | Complete |
| 4 | Persistent Notice Board API, expiry, validation, basic UI | Complete |
| 5 | Persistent Missed Connections API and basic UI | Complete |
| 6 | Private Digital Letters and status-only tracking | Complete |
| 7 | Authenticated Postie workflow and moderation | Complete |
| 8 | Proper mobile public UI and derived statistics | Complete |
| 9 | Reliability, limits, encoding, pruning, duplicate handling | Complete; hardware verification pending |
| 10 | Runtime diagnostics and multi-phone test preparation | Complete; load test pending |
| 11 | E-ink/display integration | **Forbidden until explicitly authorized** |

No display libraries, display pins, layout, refresh strategy, or placeholder
display module belong in the project yet.

## Recommended next work, excluding display

1. Implement the native test seam and high-value tests above.
2. Pin the known-good PlatformIO platform/framework versions for reproducible
   builds.
3. Add checksums/generation metadata and explicit migration/recovery behavior
   to persistent formats.
4. Decide whether reboot-extended public expiry is acceptable; otherwise add an
   admin-set festival clock or battery-backed RTC.
5. Add a deployment guard that refuses Postie access while the default password
   remains configured.
6. Decide letter retention/deletion policy and whether tracking codes need a
   larger, less enumerable namespace.
7. Prototype the read-only SD asset provider behind a compile-time flag after
   choosing the exact card hardware and wiring.
8. Test whether `.local` is reliable with wildcard unicast DNS on target phones;
   `.local` is commonly treated as mDNS-special.
9. Perform power-cut, storage-corruption, captive-device, soak, and 1/2/4/8-phone
   hardware tests while watching protected diagnostics.

