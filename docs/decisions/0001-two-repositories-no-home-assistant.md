# 0001 — An API and a display, without Home Assistant

Date: 2026-10-18 (written down 2026-10-20). Status: accepted.

## Context

Vigie shows world hazards and weather on a touch screen. The data comes from many public
sources with different formats, rate limits and sizes (some answer with megabytes of JSON).

## Decision

- Two repositories: **vigie-api** gathers and merges the sources and serves a compact frame;
  **vigie-display** (this one) only fetches and shows that frame.
- The display does not depend on Home Assistant or any home automation server: no `api:`
  component, no Home Assistant entity.
- The firmware reuses the board knowledge of the M5-Tab5-ESPHome-LVGL project (panel power,
  Wi-Fi link reset, touch settings) but none of its user interface code.

## Consequences

- The display needs vigie-api reachable on the network (today: a PC on the LAN).
- The frame contract is shared: vigie-api owns it (`docs/embedded-frame.md`), the display tests
  it against a real captured frame.
