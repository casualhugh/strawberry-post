# Architecture and behaviour

## Network behaviour

- AP SSID: `STRAWBERRY POST no internet`
- AP address and gateway: `192.168.4.1/24`
- HTTP: port 80
- Wildcard DNS: port 53, resolving names to the ESP32 with a one-second TTL
- Friendly URL: `http://post.local/`
- Direct fallback: `http://192.168.4.1/`
- Initial association limit: eight clients

The eight-client value limits Wi-Fi associations; it does not control radio
range and is not proof of eight-phone HTTP throughput. Android, Apple, and
Windows captive-network probe paths are handled as a convenience. Requests
arriving with an arbitrary hostname are redirected over HTTP to
`http://post.local/` with caching disabled. Direct requests for `post.local` or
`192.168.4.1`, including valid assets and APIs, are served normally. The product
must remain usable when a phone does not open a captive window automatically.

## Data models and current limits

All text limits below are UTF-8 byte limits. Browser `maxlength` values count
characters/code units and may therefore differ from the server's byte limit.

| Record | Fields | Capacity and limits |
| --- | --- | --- |
| Notice | ID, category enum, message, absolute/pending creation data, boot ID, hidden flag | Card capacity; latest 32 cached; message 512 bytes; hidden publicly after 48 hours |
| Letter | ID, tracking code, recipient, likely location, private message, optional sender, creation data, status | Card capacity within tracking namespace; latest 32 cached; recipient 160 bytes; location 96 bytes; message 768 bytes; sender 96 bytes |

Notice categories are an enum with the external names `General`,
`Missed Connection`, `Lost & Found`, `Event / Schedule`, `Ride Share`,
`Help Wanted`, and `For Sale / Swap`. The firmware validates them independently
of the browser dropdown.

Letter statuses are `Waiting`, `Written`, `OutForDelivery`, `Delivered`, and
`CouldNotFind`. Public tracking returns only the tracking code and status. It
never returns recipient, location, message, or sender. Tracking codes use the
friendly form `STRAW-0427` and are not cryptographic secrets.

Statistics are derived from stored activity rather than manually edited.

## Time semantics

There is no NTP or guaranteed battery-backed real-time clock. An authenticated
Postie button supplies current UTC epoch seconds from the browser. The running
device advances that value using `millis()`; after a complete power loss the
Postie sets it again.

Records created while the clock is set immediately receive an absolute creation
time. Records created earlier in the same boot are backfilled by subtracting
their uptime age from the browser time. A pending record from an earlier boot is
treated as newly created when the clock is next set because its elapsed powered-
off time cannot be known. While the clock is unset, pending notices remain
visible and no age-based deletion or hiding occurs. Once set, notices at least
48 hours old are filtered from public web and e-paper views but remain stored;
manual moderation is an independent flag and is never undone by setting time.

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
        |     +-- binary system state and boot count
        |     +-- per-letter JSON fallback when no SD card is present
        |
        +-- SD/TF card (preferred record backend)
              +-- one JSON file per notice
              +-- one JSON file per private letter
              +-- rebuildable index metadata
```

Important modules:

- `src/app_config.h`: deployment and resource limits.
- `src/main.cpp`: startup ordering and cooperative main loop.
- `src/wifi_manager.*`: deterministic SoftAP configuration.
- `src/dns_server.*`: wildcard local DNS.
- `src/web_server.*`: server setup, captive routes, and fallback routing.
- `src/public_ui.*`: public asset routes and statistics.
- `web/`: canonical, human-readable HTML/CSS/JavaScript sources.
- `tools/generate_web_assets.py`: deterministic PROGMEM bundle generator.
- `tools/dev_server.py`: standard-library desktop server and in-memory API mock.
- `src/generated_web_assets.*`: generated firmware assets; never edit directly.
- `src/notices.*`: notice model, persistence, routes, and moderation hooks.
- `src/letters.*`: private letters, tracking, status, and admin hooks.
- `src/admin.*`: Basic-authenticated Postie UI and APIs.
- `src/device_time.*`: browser-set volatile device time.
- `src/storage.*`: SD/LittleFS selection, mounting and replacement/recovery.
- `src/web_utils.*`: JSON escaping, UTF-8 validation and request policy.
- `src/diagnostics.*`: request counters and runtime health reporting.
- `src/epaper_display.*`: notice/QR display snapshots and CrowPanel refreshes.
- `lib/epaper_core/`: hardware-independent display logic.
- `lib/crowpanel_epaper/`: vendored panel driver and fonts.
- `lib/strawberry_core/`: allocation-free domain/storage algorithms shared by
  firmware and native tests.

The HTTP server is synchronous. Several phones can remain associated with the
AP, but requests are handled one at a time. Flash writes, slow clients, directory
scans and e-paper refreshes can delay HTTP and DNS work; this must be measured on
real hardware before considering an asynchronous design.

## Persistence

LittleFS is always attempted for the small binary `/system.dat` boot-state file.
Record storage is selected once during boot:

- With a usable SD card, `/notices/*.json` and `/letters/*.json` both use the
  card. Any LittleFS letters are ignored for that boot; the backends are not
  synchronized or merged.
- Without a usable SD card, notices have no persistent backend and notice
  mutations are rejected. Letters alone use `/letters/*.json` on LittleFS.
- If the selected SD card disappears or an SD operation fails, both stores keep
  their bounded in-memory data readable but latch read-only until reboot. They
  never switch to LittleFS mid-boot.

Each record is a UTF-8 JSON object with a format name and schema version. Notice
filenames are zero-padded numeric IDs; letter filenames are tracking codes. The
`index.json` in each directory contains rebuildable counters, while individual
records are the source of truth. Deserialization rejects oversized files, wrong
or extra fields, incompatible types/versions, invalid UTF-8/control characters,
filename/content mismatches, duplicate identifiers/tracking codes, and unknown
categories/statuses. There is no migration from the former native snapshots
because no real festival data exists yet.

The JSON is for computer reading and backup, including Unicode and multiline
letters. Computer-side editing is unsupported; a modified file that fails exact
schema and semantic validation is rejected. Letter files contain private content
in plain text. Power the device off before intentionally removing the card, and
reinstall it before booting if the SD dataset should be selected.

Writes use a temporary JSON file and backup rename sequence. Reads validate each
candidate before accepting it, so an invalid primary can fall back to a valid
backup. A backup-only recovery copy is retained until a new primary is promoted.
If recovery cleanup or an operation fails on SD, the selected SD backend becomes
read-only. LittleFS refuses to format a non-blank partition after mount failure;
an apparently blank first-use partition may be formatted. These rules and the
in-memory recovery tests are not proof of FAT/SD or LittleFS behaviour during a
physical power cut; both backends require hardware fault-injection testing.

RAM use stays bounded: the latest 32 notices and letters form the working cache,
public and Postie history is returned in small pages, and older records are
loaded only as needed. This removes the former 32-record storage limit; practical
SD capacity depends on free space and FAT directory performance. Notice IDs are
32-bit. The `STRAW-0000` through `STRAW-9999` format caps letter records at
10,000 distinct tracking files unless the namespace changes. Directory scans
and startup reconstruction are linear in retained record count, so large-volume
performance still needs real-card measurement.

Notices are never deleted by age. Letters also have no time-based expiry. The
Postie can permanently delete either record; a deleted letter's public tracking
result immediately becomes not found.

## Degraded-mode interface

Public pages expose no SD-card or internal-flash terminology. If a service
cannot accept writes, its form remains visible but every control is disabled and
a plain-language, Australian-humour message explains that nothing new can be
saved. Submission handlers still translate a storage failure that occurs after
page load into the same safe wording and explicitly say when a record was not
saved. If SD failure limits tracking to the RAM cache, the tracking page warns
that it can check only letters already on the desk.

The authenticated Postie page is deliberately direct. It reports each selected
backend, whether it is readable and writable, whether failure happened after
startup, what data remains available, and that the device must be powered off
before checking or installing the SD card. Letter and moderation controls are
disabled independently according to their store's writable state.
