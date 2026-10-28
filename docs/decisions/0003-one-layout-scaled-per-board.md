# 0003 — One layout, scaled per board

Date: 2026-10-28. Status: accepted.

## Context

The screen was drawn for the Waveshare 7B only (1024×600, coordinates written in
`vigie_screen.cpp`). The M5Stack Tab5 has a 1280×720 screen on a portrait panel, a higher pixel
density (5" instead of 7") and the same ESP32-P4 and ESP32-C6. ADR 0002 expected "a new set of
coordinates" per screen size.

## Decision

- **One layout, scaled.** `vigie_screen.cpp` keeps the 1024×600 numbers; each one goes through
  `sc()`, which multiplies it by the board's `board_layout_scale` (percent). What sits on a
  right edge (clock, distances, temperatures, the zone index) is placed from that edge, so the
  map and the weather panel share the rest. At 100 the result is the 7B layout, pixel for pixel.
- **Map at a fixed projection.** The map is always 360° × 144° (2.5 : 1); its size is per board
  (7B 664×266, Tab5 800×320), with one image per size drawn by `tools/make_world_map.py`.
- **Fonts per board.** Their sizes are substitutions of the board (Tab5: 120 % of the 7B ones),
  so that text keeps a readable physical size on the denser screen.
- **Geometry apart from hardware.** `firmware/packages/layout-<board>.yaml` holds only sizes and
  rotation; `board-<board>.yaml` holds the hardware and includes its layout. The off-device
  render (`vigie-render.yaml`) uses the layout without the hardware.
- **Checked at build time.** `static_assert`s refuse a layout whose alert rows or forecast would
  overflow their panels, or a weather panel too narrow; CI compiles and renders every board.
- **`vigie_board` is the API's `device_type`.** The board is sent with each request, so a board
  name is one vigie-api accepts (`waveshare-7b`, `tab5`).

## Consequences

- A new board is two YAML files and a map image, with no C++ change, as long as its screen is
  landscape-ish and about 1.7 : 1. A very different shape (portrait, square) would need its own
  layout.
- The Tab5 shows what the 7B shows, larger, with a wider weather panel; it does not show more.
- Font sizes and the scale are chosen by eye on the render, then checked on the board.
