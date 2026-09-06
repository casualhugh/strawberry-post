# CrowPanel 5.79-inch E-Paper hardware map

This project is intended to run on a CrowPanel 5.79-inch E-Paper display with a
stated panel resolution of **272 × 792 pixels**. Depending on display rotation,
software may describe the same panel as 792 × 272 pixels.

This pin map records the board information supplied for the project. Confirm it
against the exact board revision's schematic before connecting external
hardware or enabling display code.

## User controls

| Control | ESP32 GPIO |
| --- | ---: |
| Menu button | GPIO 2 |
| Rotary switch — up | GPIO 6 |
| Rotary switch — down | GPIO 4 |
| Rotary switch — select | GPIO 5 |
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
|
+-- E-paper SPI
    +-- BUSY ................ GPIO 48
    +-- RESET ............... GPIO 47
    +-- D/C ................. GPIO 46
    +-- CS .................. GPIO 45
    +-- CLK / SCK ........... GPIO 12
    +-- MOSI ................ GPIO 11
```

## Integration status

This file is documentation only. Display, input, and TF-card drivers are not yet
implemented, and no assumptions about active-low inputs, pull-ups, SPI speed,
display controller, waveform/LUT, or power sequencing have been made.
