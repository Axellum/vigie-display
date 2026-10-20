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

`python tools/make_world_map.py` redraws `firmware/images/world_map.png` from Natural Earth data
and the projection constants of `firmware/core/vigie_text.h`; `--check` (run by pytest) fails
if the image is out of date.

## Firmware

ESPHome 2026.9.1 or newer. On Windows, run ESPHome from PowerShell (not Git Bash).

```bash
esphome -s vigie_api_url http://<api-host>:8080 -s vigie_signing_key <key.pem> config vigie.yaml
esphome -s vigie_api_url http://<api-host>:8080 -s vigie_signing_key <key.pem> compile vigie.yaml
```

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
compile (minimum version from `min_version:` of `vigie.yaml`, throw-away signing key) when the
firmware changes.
