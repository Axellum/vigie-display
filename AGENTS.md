# AGENTS.md — guide for AI coding agents

Vigie display: ESPHome + LVGL 9.5 firmware for an ESP32-P4 screen (Waveshare 7B, 1024×600) that
shows world hazards and weather fetched from vigie-api. No Home Assistant. Read `README.md` and
`docs/architecture.md` first.

## Commands

```bash
python -m pytest                      # repository rules + C++ tests (needs g++/clang++ or `pip install ziglang`)
python tools/make_world_map.py        # redraw the world map after changing the projection
esphome -s vigie_api_url http://<host>:8080 -s vigie_signing_key <key.pem> compile vigie.yaml   # ESPHome >= 2026.9.1
```

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
7. Board facts in `firmware/packages/board-waveshare-7b.yaml` and `firmware/display/` were tested on
   the real board: do not change a pin, the C6 CORE reset, the touch reset pin or the panel power
   modes without a new test on the board.
8. Never commit a signing key (`*.pem`) or `secrets.yaml`. Flashing a device is a human decision.
9. English everywhere (code, comments, docs, commits). Branch + pull request; update `CHANGELOG.md`.
