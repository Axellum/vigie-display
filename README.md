# Vigie display

World hazards and local weather on an ESP32-P4 touch screen, without Home Assistant.

Vigie is a personal, non-commercial project in two parts:

- **vigie-api** — a Python service that gathers earthquakes, tsunamis, volcanoes, cyclones, floods,
  wildfires, space weather and weather forecasts from public sources, merges them and serves a
  compact text frame.
- **vigie-display** (this repository) — ESPHome + LVGL firmware that fetches that frame over the
  local network every minute and shows it.

## What the screen shows

| Area | Content |
| :--- | :--- |
| Status bar | Link to the API (up to date, waiting, unreachable — with the age of the data), date and time of the active zone |
| World map | Active hazards (dot colour = severity: red, orange, yellow, green) and the watched zones |
| Alert list | The five most severe hazards: type, magnitude when defined, source title, distance and direction from the active zone, age |
| Weather panel | Active zone: current weather, wind, gusts, humidity, rain in the next hour, air quality, waves (coastal zones), five-day forecast. Touch it to show the next zone |

Texts are in English, or in French with `-s vigie_language fr`. Values the API does not know are
left out, never invented.

## Hardware

| Board | Status |
| :--- | :--- |
| Waveshare ESP32-P4-WIFI6-Touch-LCD-7B (7", 1024×600) | Supported (`vigie_board: waveshare-7b`) |

Notes on the board: [docs/hardware.md](docs/hardware.md).

## Build and flash

Requirements: ESPHome 2026.9.1 or newer, and an RSA 3072 key that signs the firmware
(`openssl genrsa -out vigie_signing_key.pem 3072`; `*.pem` is ignored by git, keep it safe: every
later update must be signed with the same key).

```bash
esphome -s vigie_api_url http://192.168.0.155:8080 -s vigie_signing_key vigie_signing_key.pem compile vigie.yaml
```

| Substitution | Default | Meaning |
| :--- | :--- | :--- |
| `vigie_api_url` | `http://vigie-api.local:8080` | Base URL of vigie-api |
| `vigie_api_interval` | `60s` | Refresh period |
| `vigie_language` | `en` | Screen language: `en` or `fr` |
| `vigie_board` | `waveshare-7b` | Board package (`firmware/packages/board-<name>.yaml`) |
| `vigie_signing_key` | `vigie_signing_key.pem` | Firmware signing key |
| `vigie_version` | `0.1.0-dev` | Firmware version (the release tag) |

Wi-Fi credentials are not in the configuration: give them after the first flash with Improv
(USB, e.g. [ESPHome Web](https://web.esphome.io)) or through the `Vigie setup` access point. More in
[docs/development.md](docs/development.md).

## Repository layout

| Path | Content |
| :--- | :--- |
| `vigie.yaml` | ESPHome entry point |
| `firmware/core/` | Pure C++ (no ESPHome, no LVGL), tested on a PC: world state, frame decoding, texts and formatting, API client logic |
| `firmware/display/` | LVGL screen and board helpers (panel power, C6 Wi-Fi link) |
| `firmware/packages/` | ESPHome packages: board, screen, API client |
| `firmware/fonts/`, `firmware/images/` | Fonts (licences in `LICENSES.md`) and the world map |
| `tools/` | C++ tests, world map generator and its data |
| `tests/` | pytest guards and the API frame fixture |
| `docs/` | Architecture, hardware, frame contract, development, decisions |

## Licences and data

Fonts: see [firmware/fonts/LICENSES.md](firmware/fonts/LICENSES.md). Map: Natural Earth (public domain,
[tools/data/SOURCES.md](tools/data/SOURCES.md)). Hazard and weather data come from the sources
listed by vigie-api, under their own terms.
