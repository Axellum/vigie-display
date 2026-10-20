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
| Display | `firmware/display/vigie_screen.*` — the LVGL screen, built once in C++, repainted with compare-first writes | core, LVGL, ESPHome fonts/images | compiled by ESPHome |
| | `firmware/display/panel_power.*`, `c6_link.*`, `board_7b_version.h` — board helpers | ESP-IDF, ESPHome | compiled; tested on the board in the Tab5 project |
| Packages | `firmware/packages/*.yaml` — board, screen (fonts, image, LVGL, 1 s repaint), API client (HTTP every `vigie_api_interval`) | ESPHome | `esphome config` / `compile` in CI |

`tests/test_firmware.py` keeps these boundaries: core files include only the standard library
and each other, every C++ file is listed in an `includes:`, every MDI icon is in a font, and the
display never talks to Home Assistant.

## Robustness

- A response is accepted only if it is complete: HTTP 200, smaller than the receive buffer,
  ending with `END|<CRC32>` and with a matching CRC. Otherwise the screen keeps the previous
  data and the status bar shows how old it is.
- The zone the user is looking at survives every refresh (matched by id).
- The time comes from SNTP; before it is set, from the server time of the last frame.
- Unknown values (no weather for a zone, unknown rain probability or air quality) are hidden.

## Not done yet

See [roadmap.md](roadmap.md): hazard detail and alarm screen with sound, NVS cache at boot,
settings on the device, GPS receiver, other boards.
