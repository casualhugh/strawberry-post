# Architecture and behaviour

## Network behavior

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

## Data models and current limits

All text limits below are UTF-8 byte limits. Browser `maxlength` values count
characters/code units and may therefore reject or accept a different number of
emoji before the server applies its byte limit.

| Record | Fields | Current capacity and limits |
| --- | --- | --- |
| Notice | ID, category, message, creation/expiry uptime, boot ID, hidden flag | 32 records; category 32 bytes; message 512 bytes; 48 hours |
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

## Time semantics

There is no NTP or guaranteed real-time clock. Public expiry uses `millis()`
and a boot ID. On reboot, currently stored public posts receive a new 48-hour
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
- `src/epaper_display.*`: display snapshots and CrowPanel refreshes.
- `lib/epaper_core/`: hardware-independent display text and refresh logic.
- `lib/crowpanel_epaper/`: vendored panel driver and fonts.
- `lib/strawberry_core/`: allocation-free C++11 domain and storage algorithms
  shared by firmware and desktop tests, with no Arduino dependency.

The HTTP server is intentionally synchronous. Several phones can remain
associated with the AP, but HTTP requests are handled one at a time. Flash
writes, slow clients, and large responses can delay other HTTP and DNS work;
this must be characterized on real hardware before considering an async server.

## Persistence

LittleFS is mounted on an internal ESP32 flash partition.
The four-character tags below are Strawberry Post file-format sentinels. Each
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
