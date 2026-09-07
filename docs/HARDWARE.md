# CrowPanel 5.79-inch E-Paper hardware map

This project targets the Elecrow CrowPanel 5.79-inch E-Paper HMI display
(product `DIS08792E`). It contains an `ESP32-S3-WROOM-1-N8R8` module and a
black-and-white **272 × 792 pixel** panel. Depending on display rotation,
software may describe the same panel as 792 × 272 pixels.

The project-owned PlatformIO board definition is
[`boards/crowpanel_579_epaper.json`](../boards/crowpanel_579_epaper.json). It
selects the module's 8 MB QSPI flash, 8 MB octal-SPI PSRAM, 240 MHz CPU, generic
ESP32-S3 Arduino pin variant, and UART upload through the board's USB-to-serial
bridge. Explicit application pin assignments below take precedence over generic
Arduino variant defaults.

## Confirmed board characteristics

| Item | Configuration |
| --- | --- |
| ESP32 module | ESP32-S3-WROOM-1-N8R8 |
| Flash | 8 MB, QSPI |
| PSRAM | 8 MB, octal SPI (OPI) |
| CPU | Up to 240 MHz |
| E-paper | 5.79-inch, black/white, 272 × 792 |
| Display controller | Two SSD1683 controllers |
| Host interfaces | E-paper and TF card use separate SPI buses |

## User controls

| Control | ESP32 GPIO |
| --- | ---: |
| Menu button | GPIO 2 |
| Rotary switch, up | GPIO 6 |
| Rotary switch, down | GPIO 4 |
| Rotary switch, select | GPIO 5 |
| Exit button | GPIO 1 |

## TF card slot (SPI)

| TF card signal | ESP32 GPIO |
| --- | ---: |
| CS | GPIO 10 |
| MOSI | GPIO 40 |
| CLK / SCK | GPIO 39 |
| MISO | GPIO 13 |

## E-paper panel (SPI)

| E-paper signal | ESP32 GPIO |
| --- | ---: |
| BUSY | GPIO 48 |
| RESET (`RES`) | GPIO 47 |
| D/C (data/command) | GPIO 46 |
| CS | GPIO 45 |
| CLK / SCK | GPIO 12 |
| MOSI | GPIO 11 |

The TF card and e-paper panel use different SPI pin sets. The e-paper interface
does not list a MISO line because the panel is written to rather than read from.

## Board power and indicator controls

| Function | ESP32 GPIO | Active state |
| --- | ---: | --- |
| E-paper 3.3 V enable | GPIO 7 | HIGH |
| TF-card 3.3 V enable | GPIO 42 | HIGH |
| Power LED control | GPIO 41 | Board-defined |

The display and TF-card drivers must assert their respective power-enable GPIO
before initializing either SPI peripheral. GPIO 41 is documented here to avoid
an unexplained pin in future code; the application does not currently control
the power LED.

## Compact board map

```text
CrowPanel 5.79-inch E-Paper (272 × 792)
|
+-- Controls
|   +-- Menu ................ GPIO 2
|   +-- Rotary up ........... GPIO 6
|   +-- Rotary down ......... GPIO 4
|   +-- Rotary select ....... GPIO 5
|   +-- Exit ................ GPIO 1
|
+-- TF card SPI
|   +-- CS .................. GPIO 10
|   +-- MOSI ................ GPIO 40
|   +-- CLK / SCK ........... GPIO 39
|   +-- MISO ................ GPIO 13
|   +-- 3.3 V enable ........ GPIO 42 (HIGH)
|
+-- E-paper SPI
|   +-- BUSY ................ GPIO 48
|   +-- RESET ............... GPIO 47
|   +-- D/C ................. GPIO 46
|   +-- CS .................. GPIO 45
|   +-- CLK / SCK ........... GPIO 12
|   +-- MOSI ................ GPIO 11
|   +-- 3.3 V enable ........ GPIO 7 (HIGH)
|
+-- Indicator
    +-- Power LED control ... GPIO 41
```

## Integration status

The PlatformIO target now matches the installed ESP32-S3 module and memory.
The e-paper driver is implemented from Elecrow's matching dual-SSD1683 example,
including its software SPI pins and GPIO 7 power enable. Its orientation,
refresh timing, ghosting, and shutdown sequencing still require verification on
the physical panel. Input and TF-card drivers are not yet implemented.

## Hardware references

- [Elecrow CrowPanel 5.79-inch hardware wiki](https://static-cdn.elecrow.com/wiki/CrowPanel_ESP32_E-paper_5.79-inch_HMI_Display.html)
- [Elecrow Arduino tutorial](https://media-cdn.elecrow.com/wiki/CrowPanel_ESP32_E-Paper_5.79inch_Arduino_Tutorial.html)
- [Elecrow example code and schematic repository](https://github.com/Elecrow-RD/CrowPanel-ESP32-5.79-E-paper-HMI-Display-with-272-792)
- [Espressif ESP32-S3-WROOM-1/1U datasheet](https://documentation.espressif.com/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf)
