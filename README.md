# Strawberry Post

Strawberry Post is a completely offline festival web application hosted by an
ESP32-S3. The device creates its own Wi-Fi network. Attendees connect directly
with their phones and use a small local website; no internet service, cloud
account, or upstream router is required.

The project currently implements Stages 0–10 below. Display hardware is
explicitly deferred and must not be added without a separate decision.

The selected CrowPanel display and its supplied GPIO assignments are recorded
in [`HARDWARE.md`](HARDWARE.md). That document is a static wiring reference;
display and input drivers remain deferred.

## Product specification

The public application has two services:

1. **Notice Board:** the landing experience for short-lived public notices.
   Categories are selected from a fixed list; `Missed Connection` is one of
   those categories and is suggested to people looking for someone they met.
2. **Letters:** private messages submitted online, physically carried by the
   Postie, and tracked publicly by status only.

The unlinked Postie interface allows an authenticated operator to read private
letters, update delivery status, permanently delete letters, and moderate
public posts.

The visual language is a temporary riverside post office inside the Strawberry
Fields music festival on the NSW/Victoria border. It combines festival signage,
river-country colours, paper claim tickets, and a timber-and-cork community
board. It uses large mobile controls with no external fonts, CDNs, or JavaScript
frameworks. All current web assets are compiled into program flash (`PROGMEM`)
so the public shell remains available if data storage fails.

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
| Letter | ID, tracking code, recipient, likely location, private message, optional sender, creation data, status | 32 records; recipient 160 bytes; location 96 bytes; message 768 bytes; sender 96 bytes |

New notices must use one of: `General`, `Missed Connection`, `Lost & Found`,
`Event / Schedule`, `Ride Share`, `Help Wanted`, or `For Sale / Swap`. The
firmware validates the category independently of the browser dropdown.

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
        |     +-- generated HTML, CSS, and JavaScript bundle
        |
        +-- internal flash (LittleFS)
              +-- system state and boot count
              +-- notice snapshot
              +-- private-letter snapshot
```

Important modules:

- `src/app_config.h`: deployment and resource limits.
- `src/main.cpp`: startup ordering and cooperative main loop.
- `src/wifi_manager.*`: deterministic SoftAP configuration.
- `src/dns_server.*`: wildcard local DNS.
- `src/web_server.*`: server setup, captive routes, and fallback routing.
- `src/public_ui.*`: public asset routes and statistics.
- `web/`: canonical, human-readable HTML/CSS/JavaScript sources.
- `tools/generate_web_assets.py`: deterministic PROGMEM bundle generator,
  invoked automatically by the ESP32 build.
- `tools/dev_server.py`: standard-library desktop server and in-memory API mock.
- `src/generated_web_assets.*`: generated firmware asset declarations/data;
  never edit these files directly.
- `src/notices.*`: notice model, persistence, routes, and moderation hooks.
- `src/letters.*`: private letters, tracking, status, and admin hooks.
- `src/admin.*`: Basic-authenticated Postie UI and APIs.
- `src/storage.*`: LittleFS mounting and replacement/recovery helpers.
- `src/storage_format.h`: readable application file-signature construction.
- `src/web_utils.*`: JSON escaping, UTF-8 validation, request policy, and
  duplicate-submission hashing.
- `src/diagnostics.*`: request counters and runtime health reporting.
- `lib/strawberry_core/`: allocation-free C++11 domain and storage algorithms
  shared by firmware and desktop tests, with no Arduino dependency.

The HTTP server is intentionally synchronous. Several phones can remain
associated with the AP, but HTTP requests are handled one at a time. Flash
writes, slow clients, and large responses can delay other HTTP and DNS work;
this must be characterized on real hardware before considering an async server.

## Persistence and the “magic numbers”

LittleFS is mounted on an internal ESP32 flash partition. LittleFS does **not**
require the application’s `STPS`, `NOTC`, or `LETR` values.

Those four-character tags are Strawberry Post file-format sentinels. Each
binary snapshot contains an application magic value and schema version so the
firmware can reject a wrong file type or an incompatible layout instead of
interpreting arbitrary bytes as records. `makeStorageMagic()` keeps the tag
readable in code instead of duplicating unexplained hexadecimal constants.

Current files are:

- `/system.dat`: `STPS`, schema 1
- `/notices.dat`: `NOTC`, schema 1
- `/letters.dat`: `LETR`, schema 1

Files are native fixed C++ binary snapshots. That is compact and bounded, but
it couples data compatibility to struct layout, capacity, compiler ABI, and
schema version. There is currently no checksum or migration layer. Any future
record-layout change must deliberately bump the version and define whether old
festival data is migrated, exported, or discarded.

Writes use a temporary file and backup rename sequence. Reads validate each
candidate before accepting it, so an exact-sized but semantically invalid
primary can fall back to a valid backup. A backup-only recovery copy is retained
until a new primary is promoted. After a backup wins validation, the rejected
primary is removed before later writes; if that removal fails, persistence is
disabled rather than risking the valid backup. If LittleFS fails to mount and
the partition appears non-blank, firmware refuses to format it. An apparently
blank first-use partition may be formatted. The public UI remains in program
flash, but new submissions return a storage-unavailable response.

Expired public-record compaction is transactional in RAM: if the replacement
write fails, the original count and byte order are restored so a later access
can retry. Expired records are still filtered from public results during that
failure. This is deterministic logic, not proof of LittleFS behavior during a
physical power cut.

The notice store prunes its oldest record when full. A full letter store prunes
the oldest completed/failed letter; if all 32 letters are still active, a new
letter is rejected.

Letters have no time-based expiry. The Postie decides when a letter is no
longer needed and can permanently delete it from the sorting room. Public
tracking for a deleted letter immediately returns not found. Tracking numbers
remain four digits because the expected three-day festival volume is well
within the available 10,000-code namespace.

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
| GET | `/` | Notice Board landing page, posting form, and derived summary |
| GET | `/style.css` | Flash-resident public stylesheet |
| GET | `/logo.svg` | Flash-resident Strawberry Post logo |
| GET/POST | `/api/notices` | List/create notices |
| GET | `/letters` | Private letter-writing postcard page |
| GET | `/track` | Public tracking-number lookup page |
| POST | `/api/letters` | Create a private letter |
| GET | `/api/letters/status?tracking=...` | Public status-only tracking |
| GET | `/api/stats` | Derived public statistics |
| GET | `/postie` | Authenticated sorting room |
| GET | `/postie/diagnostics` | Authenticated diagnostics page |
| GET | `/api/admin/overview` | Private letters and moderation data |
| POST | `/api/admin/letters/status` | Change letter status |
| POST | `/api/admin/letters/delete` | Permanently delete a letter |
| POST | `/api/admin/notices/moderate` | Hide/unhide/delete a notice |
| GET | `/api/admin/diagnostics` | Runtime diagnostic JSON |

## Build

PlatformIO environment:

- Platform: `espressif32`
- Board: `crowpanel_579_epaper` (project-owned definition for the Elecrow
  CrowPanel 5.79-inch E-Paper and its ESP32-S3-WROOM-1-N8R8 module)
- Framework: Arduino
- Environment: `esp32s3`
- Filesystem: LittleFS

The custom board manifest in `boards/crowpanel_579_epaper.json` configures 8 MB
QSPI flash, 8 MB OPI PSRAM, and serial upload through the panel's USB-to-UART
bridge. See [HARDWARE.md](HARDWARE.md) for the verified board pins and power
controls.

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

## Desktop browser preview

The files under `web/` are the single source of truth for the browser UI. The
firmware build regenerates `src/generated_web_assets.*` and embeds the result in
PROGMEM, so moving the editable source out of C++ does not introduce a runtime
filesystem read or slow page delivery on the ESP32.

Run the local preview from the repository root:

```powershell
C:\Users\Hughe\.platformio\penv\Scripts\python.exe tools\dev_server.py
```

Then open `http://127.0.0.1:8080/`. The Postie preview at `/postie` deliberately
uses the same development Basic Auth credentials as the firmware:

```text
username: postie
password: change-me-postie
```

The preview implements the categorized Notice Board, letter submission and
tracking, statistics, authenticated Postie status/moderation, and diagnostics.
Its seeded records and new submissions live only in RAM and reset whenever the
server stops. It models HTTP/UI behavior; it does not emulate Wi-Fi, DNS,
LittleFS, flash timing, expiry across reboot, or power loss.

PlatformIO regenerates assets during the ESP32 build. To regenerate them
explicitly while editing the UI, run:

```powershell
C:\Users\Hughe\.platformio\penv\Scripts\python.exe tools\generate_web_assets.py
```

## Native tests before hardware

The repository now has two PlatformIO/Unity desktop suites containing 23 test
functions under `test/native/`. Production firmware calls the same
platform-neutral implementations; the tests do not carry a second copy of the
algorithms. Explicit `nowMs` arguments provide a fake-clock seam, while an
injected memory file backend supplies deterministic read, write, remove, rename,
corruption, and failure behavior.

Current automated coverage includes:

- expiry just before, at, and after a deadline, including `millis()` rollover;
- UTF-8 scalar boundaries, four-byte emoji, controls, truncation, overlong
  forms, surrogates, invalid continuations, and values above U+10FFFF;
- deterministic FNV-1a field hashing and wrap-safe duplicate-window timing;
- tracking formatting, occupied-code search, `9999` to `0000` wrap, exhaustion,
  and a cursor that is not mutated until allocation succeeds;
- stable expiry compaction and byte-for-byte RAM rollback after commit failure;
- primary/backup semantic validation and exact-sized corrupt-primary fallback;
- rejected-primary cleanup and preservation of the valid backup when the next
  temporary-to-primary promotion fails;
- atomic replacement success plus failures while writing, removing a stale
  backup, renaming the primary, promoting the temporary, and restoring backup;
- preservation of a readable recovery copy when only a backup exists.

Run the suites with a host GCC/G++ compiler available on `PATH`:

```powershell
$env:PYTHONUTF8 = '1'
C:\Users\Hughe\.platformio\penv\Scripts\platformio.exe test --environment native
```

PlatformIO's `native` platform does not install a compiler. On Windows, install
a current MinGW-w64 toolchain (for example MSYS2 UCRT64 GCC) and prepend its
`bin` directory to `PATH`. The development machine uses MSYS2 UCRT64 GCC 16.1.0.
On 6 September 2026, PlatformIO built and executed both suites successfully: all
23 test functions passed. The ESP32 build also compiles the shared library.

Useful next native cases are full-board pruning, all-active-letter rejection,
exact payload comparison after the 32-bit hash prefilter, JSON escaping,
record-ID/counter wrap, statistics, and complete endpoint/domain mutation
rollback. Those require further separation from the current HTTP handlers.

Additional pre-hardware checks:

- Build with `-Wall -Wextra` and fail CI for warnings originating in `src/`.
- Run `pio check` with an available static analyzer.
- Use the desktop preview for human browser, accessibility, viewport, and
  no-network inspection.
- Keep validating that HTML refers only to local URLs and that public schemas
  never include private letter field names.
- Add fixture-based size checks so a web bundle or persistent snapshot cannot
  silently exceed its configured budget.

An emulator can help with CPU-only logic, but it should not be treated as proof
of ESP32 SoftAP, DNS, captive WebView, LittleFS power-loss, SD wiring, flash
latency, or multiple-phone behavior. Those remain hardware-in-the-loop tests.

The desktop web integration suite verifies generated-asset freshness, local-only
references, page serving, Postie authentication, public/private letter
separation, forms, tracking, status changes, moderation, and input errors:

```powershell
C:\Users\Hughe\.platformio\penv\Scripts\python.exe -m unittest discover -s test\tools -v
```

## Removable SD web-asset proposal (not implemented)

LittleFS is designed for a flash partition attached to the ESP32. A removable,
laptop-editable SD card should instead be FAT/FAT32 and mounted with Arduino's
`SD` library over SPI or `SD_MMC` with suitable hardware wiring.

The selected CrowPanel provides an onboard TF-card slot on a dedicated SPI bus.
Its CS, MOSI, SCK, MISO, and active-high power-enable assignments are recorded
in [HARDWARE.md](HARDWARE.md). The removable web-asset design below remains a
proposal: the firmware does not yet mount the card or load assets from it.

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
| 5 | Persistent Missed Connections API and basic UI | Complete historically; later merged into Notice Board categories |
| 6 | Private Letters and status-only tracking | Complete |
| 7 | Authenticated Postie workflow and moderation | Complete |
| 8 | Proper mobile public UI and derived statistics | Complete |
| 9 | Reliability, limits, encoding, pruning, duplicate handling | Complete; hardware verification pending |
| 10 | Runtime diagnostics and multi-phone test preparation | Complete; load test pending |
| 11 | E-ink/display integration | **Forbidden until explicitly authorized** |

No display libraries, display pins, layout, refresh strategy, or placeholder
display module belong in the project yet.

## Recommended next work, excluding display

1. Add the native, web-preview, generated-asset, and ESP32 build checks to CI.
2. Extend the domain seam to cover full-store pruning, exact duplicate payload
   comparison, statistics, and complete mutation rollback.
3. Pin the known-good PlatformIO platform/framework versions for reproducible
   builds.
4. Add checksums/generation metadata and explicit migration/recovery behavior
   to persistent formats.
5. Decide whether reboot-extended public expiry is acceptable; otherwise add an
   admin-set festival clock or battery-backed RTC.
6. Prototype the read-only SD asset provider behind a compile-time flag after
   choosing the exact card hardware and wiring.
7. Test whether `.local` is reliable with wildcard unicast DNS on target phones;
   `.local` is commonly treated as mDNS-special.
8. Perform power-cut, storage-corruption, captive-device, soak, and 1/2/4/8-phone
   hardware tests while watching protected diagnostics.
