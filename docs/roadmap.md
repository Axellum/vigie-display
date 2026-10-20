# Roadmap

## Done (0.1.0, not released yet)

- Firmware compiles for the Waveshare 7B with ESPHome 2026.9.1.
- Real HTTP fetch of the API frame, strict checks (status, size, END line, CRC32).
- First screen: status bar, world map with hazards and zones, five most severe alerts, weather of
  the active zone (touch to change zone), English and French.
- C++ core tested on a PC and in CI.

## Next

1. **Try it on the board** (flash, Wi-Fi, frame received, screen and touch checked).
2. **Hazard detail**: touch an alert row to see its details (coordinates, depth, sources,
   distance in km and nautical miles, bearing, tsunami arrival estimate when relevant).
3. **Alarm**: wake the screen and play a sound for a critical hazard not acknowledged yet, with an
   acknowledge button (the decision logic exists: `vigie_evaluate_alarm`).
4. **Cache at boot**: keep the last frame in NVS and show it before the network is up
   (`vigie_nvs_pack` / `vigie_nvs_unpack` exist).
5. **Settings on the device**: API address and language without rebuilding.
6. **GPS receiver** (P3 UART or RS485, NMEA 0183): the parser exists (`vigie_parse_nmea`).
7. **Other boards**: M5Stack Tab5 (1280×720) and others, one board package and one layout each.
