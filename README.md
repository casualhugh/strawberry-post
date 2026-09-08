# Strawberry Post

An offline festival notice board and letter service hosted by an ESP32-S3
CrowPanel. Phones connect to its Wi-Fi network; no internet or router is needed.

- **Notice Board:** public, categorized notices hidden after 48 hours.
- **Letters:** private messages for the Postie, with public status-only tracking.
- **Postie:** authenticated sorting room for delivery updates and moderation.
- **E-paper:** rotating public notices and aggregate postal counts.

## Try it locally

```sh
python tools/dev_server.py
```

Open http://127.0.0.1:8080/. The preview uses temporary in-memory data.
Visit `/postie` with username `postie` and password `change-me-postie`.

## Run on the board

Change `kAdminPassword` in `src/app_config.h` before deployment, then:

```sh
pio run -e esp32s3 -t upload
pio device monitor -b 115200
```

Connect to **STRAWBERRY POST no internet** and open http://192.168.4.1/.
The friendly address http://post.local/ and automatic captive window may also work.

The website, JSON record persistence, physical navigation and e-paper driver are
implemented; web assets are embedded in firmware. Physical-panel, SD/FAT and
LittleFS power-loss, card-removal and multi-phone verification are still required.

An SD card present at boot stores notices and private letters. Without one,
notices are read-only/unavailable for submission while letters fall back to
internal flash. Power off before removing the card. The JSON files are intended
for computer reading and backup, not editing, and private letters are plain text.

## Operating limits

Each notice and letter is stored as its own JSON file. The firmware keeps the
latest 32 of each in a bounded working cache and pages older history from the SD
card. Records are retained until the Postie deletes them; notices automatically
leave public views after 48 hours once the browser-supplied device clock is set.
Letters have no time-based expiry. Tracking codes use a 10,000-code namespace.

The e-paper display shows the Wi-Fi connection QR after every three 30-second
notice slots, or continuously while there are no active notices. Change
`kEpaperNoticeSlotsPerWifiQr` in `src/app_config.h` to tune that cadence.

The Wi-Fi is open and HTTP, Basic Auth credentials and stored letters are
unencrypted. Public tracking exposes only a tracking code and delivery status.
If storage becomes unavailable, affected public forms remain visible but are
disabled with non-technical guidance. The authenticated Postie page shows the
actual backend state and recovery instructions.

## Documentation

- [Development, builds and tests](docs/development.md)
- [Architecture, data limits and persistence](docs/architecture.md)
- [HTTP routes](docs/api.md)
- [Hardware and pin assignments](docs/HARDWARE.md)
- [E-paper layout and verification](docs/EPAPER_DISPLAY.md)
