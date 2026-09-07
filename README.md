# Strawberry Post

An offline festival notice board and letter service hosted by an ESP32-S3
CrowPanel. Phones connect to its Wi-Fi network; no internet or router is needed.

- **Notice Board:** public, categorized notices that expire after 48 hours.
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

The website, persistence and e-paper driver are implemented. Physical-panel,
power-loss and multi-phone verification are still required. Buttons and the
TF-card slot are not integrated; web assets are embedded in firmware.

## Operating limits

Stores hold up to 32 notices and 32 letters. Full stores discard the oldest
notice or oldest completed/failed letter; new letters are rejected if all are
active. Rebooting restarts notices' 48-hour expiry window. Letters have no
time-based expiry and can be deleted by the Postie.

The Wi-Fi is open and HTTP, Basic Auth credentials and stored letters are
unencrypted. Public tracking exposes only a tracking code and delivery status.

## Documentation

- [Development, builds and tests](docs/development.md)
- [Architecture, data limits and persistence](docs/architecture.md)
- [HTTP routes](docs/api.md)
- [Hardware and pin assignments](docs/HARDWARE.md)
- [E-paper layout and verification](docs/EPAPER_DISPLAY.md)
