# Development

## Tests on a PC

```bash
pip install -r requirements-dev.txt
python -m pytest
```

`tests/test_firmware.py` checks the repository's rules and compiles and runs the C++ tests of
`tools/` against `firmware/core/`. It needs a host C++ compiler: g++ or clang++, or on Windows
`pip install ziglang` (used through `python -m ziglang c++`). Without one, the C++ tests are
reported as skipped, never as passed. The CI always runs them with g++.

Rules the tests hold:

- `firmware/core/` is plain C++17: no ESPHome, no LVGL, no allocation. New logic goes there,
  with a test in `tools/test_*.cpp` (and its line in `CPP_TESTS` of `tests/test_firmware.py`).
- Local `#include "x.h"` never names a folder, and a header name is unique across `firmware/`:
  ESPHome copies every `includes:` file flat into its build.
- Every C++ file of `firmware/` is listed in an `includes:` of `vigie.yaml` or a package.
- Every MDI icon drawn by the C++ is in the `glyphs:` of its font (`vigie-screen.yaml`), and
  every listed glyph is drawn: a missing glyph renders blank with no build error.
- On-screen text goes through `firmware/core/vigie_text.*`, in English and French.
- No `api:` and no Home Assistant anywhere.
- Never display an invented value: unknown is hidden.

## World map

`python tools/make_world_map.py` redraws one `firmware/images/world_map_<w>x<h>.png` per map size
of a board layout (`board_map_width` / `board_map_height` of `firmware/packages/layout-*.yaml`),
from Natural Earth data and the latitude band of `firmware/core/vigie_text.h`; `--check` (run by
pytest) fails if an image is missing, out of date or used by no board.

## Adding a board

1. `firmware/packages/layout-<board>.yaml`: the landscape screen size, the panel size, the LVGL
   rotation between them, the layout scale (percent of the 1024×600 layout), a 2.5 : 1 map size
   and the font sizes. `<board>` must be a `device_type` vigie-api accepts.
2. `firmware/packages/board-<board>.yaml`: the hardware, `packages: layout: !include layout-<board>.yaml`,
   `board_panel_off_mode` and `board_panel_dark_during_ota`, and the same ids as the other boards
   (`panel`, `touch`, `backlight`, `ota_in_progress`, the `panel_wake_up` and `panel_during_ota`
   scripts).
3. `python tools/make_world_map.py`, then `python -m pytest`.
4. Add the board to the matrices of `.github/workflows/ci.yml` and `render.yml`, look at its
   render, then try it on the board.

`static_assert`s in `vigie_screen.cpp` refuse a layout that would overflow (ADR 0003).

## Rendering without a board

`vigie-render.yaml` builds the screen for ESPHome's `host` platform (Linux or macOS: the host
platform does not build on Windows) and writes the panel as a BMP:

```bash
esphome -s vigie_board tab5 -s vigie_language fr -s vigie_frame "$PWD/tests/fixtures/api_frame.txt" compile vigie-render.yaml
ESPHOME_SNAPSHOT_DIR="$PWD/render" "$(find .esphome/build/vigie-render -name program -type f | head -n 1)"
python tools/render_png.py render tab5
```

CI does the same for every board and language with a live frame of the public API
(`.github/workflows/render.yml`, artifacts `render-<board>-<language>`); the screenshots of
`docs/images/` and of the site come from there. With the fixture, ages read as days: its
hazards are as old as the capture.

## Firmware

ESPHome 2026.9.1 or newer. On Windows, run ESPHome from PowerShell (not Git Bash).

```bash
esphome -s vigie_board tab5 -s vigie_api_url http://<api-host>:8080 -s vigie_signing_key <key.pem> config vigie.yaml
esphome -s vigie_board tab5 -s vigie_api_url http://<api-host>:8080 -s vigie_signing_key <key.pem> compile vigie.yaml
```

`vigie_board`: `waveshare-7b` (default) or `tab5`. The device name is `vigie` for every board:
its build folder is shared, so a build for another board replaces the previous one.

The signing key (RSA 3072) stays out of git. A device running firmware signed with key A only
accepts an over-the-air update signed with key A; with another key, flash over USB.

First flash over USB, then Wi-Fi with Improv (ESPHome Web) or the `Vigie setup` access point.
Later updates over the air: `esphome … upload --device <ip> vigie.yaml`.
Logs: `esphome … logs --device <COM port> vigie.yaml` (USB; the firmware has no network API).

## Refreshing the API frame fixture

When the frame contract changes in vigie-api, capture a new real frame from that repository:

```bash
python tools/capture_frame.py <path to this repo>/tests/fixtures/api_frame.txt
```

then run the tests here.

## CI

`.github/workflows/ci.yml`: pytest with g++ on every push and pull request, and an ESPHome
compile of every board (minimum version from `min_version:` of `vigie.yaml`, throw-away signing
key) when the firmware changes. `.github/workflows/render.yml`: the off-device render of every
board and language. `.github/workflows/pages.yml`: the project page (`site/`) on GitHub Pages.
Required checks on `main`: `changes` and `tests`.
