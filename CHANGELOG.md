# Changelog

All notable changes to this project are documented here. Format:
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions are git tags.

## [Unreleased]

### Added

- The Vigie logo, an eye with an orange iris, as the project page's icon (`site/favicon.svg`) and at the top
  of the README, the same as the website's.
- M5Stack Tab5 (1280×720, `-s vigie_board tab5`): board package taken from the
  M5-Tab5-ESPHome-LVGL project (ST7123 by default; ST7121 and ILI9881C by substitution), the
  screen layout scaled to 120 % with a 800×320 map (ADR 0003). Runs on a Tab5 ST7121 (Wi-Fi,
  frame from the public API over HTTPS); its ESP32-C6 is powered through the second I/O expander.
- Board geometry apart from hardware (`firmware/packages/layout-<board>.yaml`); one world map
  image per map size.
- The screen rendered off the device (`vigie-render.yaml`, ESPHome `host` platform): CI renders
  every board and language from a live frame of the public API.
- Installation guide (`docs/installation.md`), hardware page for both boards, ADR 0003, and a
  project page on GitHub Pages (`site/`).
- First real screen for the Waveshare ESP32-P4 7B (1024×600): status bar, world map with active
  hazards and watched zones, the five most severe alerts, weather of the active zone with a
  five-day forecast; touch the weather panel for the next zone.
- Real fetch of the vigie-api frame every 60 s; a response is refused unless complete (HTTP 200,
  not filling the buffer, `END|<CRC32>` line, matching CRC). The zone on screen survives refreshes.
- Unknown values stay unknown: an empty field of the frame is hidden (or reads "–" in a table),
  never shown as 0; "Alert sources unreachable" instead of "No active alert" when no hazard
  source answered the API; alert counts of every active hazard (from `HEAD`).
- English and French screen texts (`vigie_language`).
- World map drawn from Natural Earth data in the screen's projection (`tools/make_world_map.py`).
- Signed firmware updates (`vigie_signing_key`), Wi-Fi given after flashing (Improv, access point).
- Tests on a PC: C++ core (model, texts, API client against a real frame) and repository rules;
  CI with pytest (g++) and an ESPHome compile.

### Changed

- The screen layout is computed from the board's sizes; the Waveshare 7B keeps its layout pixel
  for pixel. `firmware/images/world_map.png` is now `world_map_664x266.png`.
- CI compiles the firmware for every board.
- Whole repository in English; code organised as `firmware/core` (plain C++), `firmware/display`
  (LVGL and board helpers) and `firmware/packages` (ESPHome).

### Removed

- Files copied from the M5Stack Tab5 project and not used by Vigie (themes, games, Home Assistant
  screens, their fonts and UI components), Python mock-up renderings, and the Python re-implementation
  of the C++ logic (replaced by tests of the C++ itself). The GPS package, the alarm sound and the
  hazard detail sheet will come back with real hardware tests (docs/roadmap.md).
