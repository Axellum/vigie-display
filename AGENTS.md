# AGENTS.md — guide for AI coding agents

Vigie display: ESPHome + LVGL 9.5 firmware for ESP32-P4 screens (Waveshare 7B, 1024×600; M5Stack
Tab5, 1280×720) that shows world hazards and weather fetched from vigie-api. No Home Assistant.
Read `README.md` and `docs/architecture.md` first.

## Commands

```bash
python -m pytest                      # repository rules + C++ tests (needs g++/clang++ or `pip install ziglang`)
python tools/make_world_map.py        # redraw the world maps after changing a map size or the projection
esphome -s vigie_board <waveshare-7b|tab5> -s vigie_api_url http://<host>:8080 -s vigie_signing_key <key.pem> compile vigie.yaml   # ESPHome >= 2026.9.1
```

Off-device render (Linux/macOS, or CI `render.yml`): `docs/development.md`, "Rendering without a board".

## Rules

1. Logic without LVGL goes in `firmware/core/` (plain C++17, no allocation, no ESPHome), with a
   test in `tools/test_*.cpp`. The LVGL layer (`firmware/display/vigie_screen.cpp`) only places
   texts computed by the core.
2. Every on-screen text goes through `firmware/core/vigie_text.*`, in English and French.
3. Never display an invented value: unknown is hidden or shown as unknown.
4. Every MDI icon drawn must be in its font's `glyphs:` (`firmware/packages/vigie-screen.yaml`).
5. Local includes are flat (`#include "vigie_model.h"`) and every C++ file is listed in an `includes:`.
6. The frame contract belongs to vigie-api (`docs/embedded-frame.md` there); refresh
   `tests/fixtures/api_frame.txt` when it changes.
7. Board facts in `firmware/packages/board-*.yaml` and `firmware/display/` were tested on the real
   boards (7B here, Tab5 in the M5-Tab5-ESPHome-LVGL project): do not change a pin, the C6 CORE
   reset, a touch reset, the Tab5's blocking boot delay or the panel power modes without a new
   test on the board.
8. Layout: lengths in `vigie_screen.cpp` are 1024×600 numbers passed through `sc()`; sizes per
   board live in `firmware/packages/layout-<board>.yaml` (ADR 0003). Look at the CI render of every
   board after a layout change.
9. Never commit a signing key (`*.pem`) or `secrets.yaml`. Flashing a device is a human decision.
10. English everywhere (code, comments, docs, commits). Branch + pull request; update `CHANGELOG.md`.
