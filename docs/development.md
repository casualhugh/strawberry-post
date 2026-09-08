# Development

Run commands from the repository root with Python 3.10+ and PlatformIO installed.
If `pio` is unavailable on Windows, use
`& "$env:USERPROFILE/.platformio/penv/Scripts/platformio.exe"` instead.

## Browser preview

```sh
python tools/dev_server.py
```

Open http://127.0.0.1:8080/. The `/postie` preview uses `postie` /
`change-me-postie`. Its seeded data and submissions live in memory and reset
when the server stops. It does not model notice expiry, duplicate suppression,
Wi-Fi, DNS, SD/LittleFS persistence, or display timing.

Edit the canonical assets in `web/`. The firmware build automatically generates
`src/generated_web_assets.*`; do not edit those generated files directly.
To regenerate explicitly:

```sh
python tools/generate_web_assets.py
```

## Build and upload

```sh
pio run -e esp32s3
pio run -e esp32s3 -t upload
pio device monitor -b 115200
```

The project-owned CrowPanel board definition configures 8 MB QSPI flash,
8 MB OPI PSRAM and serial upload through the USB-to-UART bridge.
See [hardware](HARDWARE.md) for pins and power controls.

## Checks

```sh
python -m unittest discover -s test/tools -v
pio test -e native
pio run -e esp32s3
```

Native tests require GCC/G++ on PATH. On Windows, use a MinGW-w64 toolchain
such as MSYS2 UCRT64 and set `PYTHONUTF8=1` if needed. Native suites cover domain
validation, expiry, tracking allocation, storage failure recovery and display
layout/refresh decisions. Web tests cover asset freshness and mock HTTP flows;
they also cover degraded-storage capability fields and Postie/public messaging,
but do not execute the firmware HTTP handlers.

Before festival use, test SD and LittleFS power cuts separately, unexpected SD
removal, booting with and without a card, storage recovery, captive portals,
1/2/4/8-phone traffic and display refresh responsiveness on the board.
See [display verification](EPAPER_DISPLAY.md#hardware-verification-still-required).
