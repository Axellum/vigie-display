# Architecture

## Data flow

```text
public sources ──► vigie-api (PC on the LAN) ──HTTP, every 60 s──► vigie-display (ESP32-P4)
 USGS, EMSC,        merges, deduplicates,      GET /v1/embedded/frame    decodes, keeps the
 GDACS, NOAA,       computes the frame          text frame + CRC32        active zone, paints
 Open-Meteo…                                                              the LVGL screen
```

The display never talks to the data sources directly: they are many, change format, and some
answer with megabytes of JSON. The API turns them into one frame of a few kilobytes that the
P4 decodes without allocating memory. Contract: [api-frame.md](api-frame.md).

## Firmware layers

| Layer | Files | Depends on | Tested by |
| :--- | :--- | :--- | :--- |
| Core | `firmware/core/vigie_model.*` — world state, geometry, NMEA, frame decoder, CRC32, NVS block, alarm decision | C++17 only | `tools/test_vigie_model.cpp` |
| | `firmware/core/vigie_text.*` — every on-screen text (English, French), numbers, dates, map projection, icons | core | `tools/test_vigie_text.cpp` |
| | `firmware/core/vigie_api_client.*` — URL, HTTP response checks, link state | core | `tools/test_vigie_api_client.cpp` (real frame fixture) |
| Display | `firmware/display/vigie_screen.*` — the LVGL screen, built once in C++, repainted with compare-first writes; layout drawn for 1024×600 and scaled per board | core, LVGL, ESPHome fonts/images | compiled by ESPHome; rendered off the device (`vigie-render.yaml`) |
| | `firmware/display/panel_power.*`, `c6_link.*`, `board_7b_version.h` — board helpers | ESP-IDF, ESPHome | compiled; tested on the boards in the Tab5 project |
| Packages | `firmware/packages/*.yaml` — boards (`board-<board>`: hardware) and their geometry (`layout-<board>`: screen, panel, rotation, map, fonts), screen (fonts, image, LVGL, 1 s repaint), API client (HTTP every `vigie_api_interval`) | ESPHome | `esphome compile` of every board in CI |

`tests/test_firmware.py` keeps these boundaries: core files include only the standard library
and each other, every C++ file is listed in an `includes:`, every MDI icon is in a font, and the
display never talks to Home Assistant.

## Boards and layouts

One firmware, one board package chosen at build time (`vigie_board`). The board package holds
the hardware (pins, Wi-Fi link, panel model, backlight, boot sequence) and includes its layout
package, which holds only sizes: the landscape screen LVGL draws, the panel as its driver sees
it, the rotation between them, the map size, the font sizes and a layout scale. The screen
package passes the sizes to the C++ as build flags and picks the map image of that size.

`vigie_screen.cpp` is drawn once, for 1024×600. On another board every length is multiplied by
the layout scale, and what sits on a right or bottom edge is placed from that edge, so the map
(a fixed 2.5 : 1 projection) and the weather panel share what is left. `static_assert`s stop a
layout whose alert rows or forecast would overflow. Details: [ADR 0003](decisions/0003-one-layout-scaled-per-board.md).

| Board | Screen | Scale | Map | Rotation |
| :--- | :--- | :--- | :--- | :--- |
| Waveshare 7B | 1024×600 | 100 % | 664×266 | 0 |
| M5Stack Tab5 | 1280×720 | 120 % | 800×320 | 270 (portrait panel) |

`vigie-render.yaml` builds the same screen for ESPHome's `host` platform with an in-memory
display and a frame read from a file: CI renders every board and language
(`.github/workflows/render.yml`), which shows a layout without flashing a board.

## Robustness

- A response is accepted only if it is complete: HTTP 200, smaller than the receive buffer,
  ending with `END|<CRC32>` and with a matching CRC. Otherwise the screen keeps the previous
  data and the status bar shows how old it is.
- The zone the user is looking at survives every refresh (matched by id).
- The time comes from SNTP; before it is set, from the server time of the last frame.
- Unknown values (no weather for a zone, unknown rain probability or air quality) are hidden.

## Not done yet

See [roadmap.md](roadmap.md): hazard detail and alarm screen with sound, NVS cache at boot,
settings on the device, GPS receiver, published firmware and a web installer.
