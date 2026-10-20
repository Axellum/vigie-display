# 0002 — The screen is built in C++, its logic is plain C++

Date: 2026-10-20. Status: accepted.

## Context

The screen repeats the same elements many times (30 map dots, 5 alert rows, 6 detail rows, 5
forecast rows) and every value it shows needs formatting (units, language, dates).

## Decision

- The LVGL objects are created once by `firmware/display/vigie_screen.cpp` and repainted with
  compare-first writes; the YAML only declares fonts, the map image and LVGL itself.
- Everything that can be computed without LVGL (decoding, texts, numbers, dates, projection,
  alarm decision) lives in `firmware/core/`, plain C++17, tested on a PC.
- Texts exist in English (default) and French, chosen at build time (`vigie_language`).

## Consequences

- ESPHome compiles an LVGL widget type only if the YAML uses it: `vigie-screen.yaml` keeps one
  hidden label and one hidden image for that reason.
- A layout for another screen size is a new set of coordinates in `vigie_screen.cpp` (today
  1024×600 only).
