# Changelog

All notable changes to this project are documented here. Format:
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions are git tags.

## [Unreleased]

### Added

- First real screen for the Waveshare ESP32-P4 7B (1024×600): status bar, world map with active
  hazards and watched zones, the five most severe alerts, weather of the active zone with a
  five-day forecast; touch the weather panel for the next zone.
- Real fetch of the vigie-api frame every 60 s; a response is refused unless complete (HTTP 200,
  not filling the buffer, `END|<CRC32>` line, matching CRC). The zone on screen survives refreshes.
- English and French screen texts (`vigie_language`).
- World map drawn from Natural Earth data in the screen's projection (`tools/make_world_map.py`).
- Signed firmware updates (`vigie_signing_key`), Wi-Fi given after flashing (Improv, access point).
- Tests on a PC: C++ core (model, texts, API client against a real frame) and repository rules;
  CI with pytest (g++) and an ESPHome compile.

### Changed

- Whole repository in English; code organised as `firmware/core` (plain C++), `firmware/display`
  (LVGL and board helpers) and `firmware/packages` (ESPHome).

### Removed

- Files copied from the M5Stack Tab5 project and not used by Vigie (themes, games, Home Assistant
  screens, their fonts and UI components), Python mock-up renderings, and the Python re-implementation
  of the C++ logic (replaced by tests of the C++ itself). The GPS package, the alarm sound and the
  hazard detail sheet will come back with real hardware tests (docs/roadmap.md).
