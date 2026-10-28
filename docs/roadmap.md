# Roadmap

## Done (0.1.0, not released yet)

- Firmware compiles for the Waveshare 7B with ESPHome 2026.9.1; run on the board on 2026-10-20.
- Real HTTP fetch of the API frame, strict checks (status, size, END line, CRC32).
- First screen: status bar, world map with hazards and zones, five most severe alerts, weather of
  the active zone (touch to change zone), English and French.
- C++ core tested on a PC and in CI.
- **M5Stack Tab5** (1280×720): board package from the Tab5 project, layout scaled from the 7B one
  (ADR 0003), compiled and rendered in CI, running on a Tab5 ST7121.
- Screen rendered off the device for every board and language (`vigie-render.yaml`).
- Installation guide and project page (GitHub Pages).

## Next

1. **Finish the Tab5 tests**: on the ST7121 (2026-10-28) the flash, Wi-Fi and frames are
   checked; screen off and on and an update over Wi-Fi are left; then an ST7123 unit.
2. **Published firmware and a web installer**: signed builds attached to a release, installed from
   the project page over USB (ESP Web Tools), with the public API by default. Needs a decision on
   the signing key of published builds.
3. **Hazard detail**: touch an alert row to see its details (coordinates, depth, sources,
   distance in km and nautical miles, bearing, tsunami arrival estimate when relevant).
4. **Alarm**: wake the screen and play a sound for a critical hazard not acknowledged yet, with an
   acknowledge button (the decision logic exists: `vigie_evaluate_alarm`).
5. **Cache at boot**: keep the last frame in NVS and show it before the network is up
   (`vigie_nvs_pack` / `vigie_nvs_unpack` exist).
6. **Settings on the device**: API address, language and own location without rebuilding.
7. **GPS receiver** (P3 UART or RS485, NMEA 0183): the parser exists (`vigie_parse_nmea`).
8. **Other boards**: one layout file and one board file each (docs/development.md).
