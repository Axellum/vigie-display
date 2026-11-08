# 0004 — The sea page: a second page of the same screen

Date: 2026-11-08. Status: accepted.

## Context

The frame can carry the sea of a surf spot (`SEA`, `ACT`, `CRIT`; ADR 0001 keeps the display on
the API only, and the core decodes it in `vigie_sea.*`). The question the screen answers is "can I
go today?" for surf, kite, boat and shore fishing, in two seconds, from a distance. The world
page (map, alerts, weather) has no room for it, and it must not lose anything of what it shows.

## Decision

- **A second page, in the same file.** The sea page is built once in `vigie_screen.cpp` like the
  rest (ADR 0002) and scaled by the same `sc()` (ADR 0003). The world page's objects sit in one
  container, the sea page's in another; one flag shows one of them. A page that is hidden is not
  repainted.
- **Switching.** A pill in the title bar, at the same place on both pages, and a swipe left or right
  anywhere. A touch that moved is never a tap (`lv_indev_get_press_moved()`), so a swipe does not
  also change the zone or open a card. The pill and the page exist only once a frame has carried a
  sea; without one the world page stays and nothing is offered.
- **What the page shows.** A banner (the coastal warning, or what is wrong with the data), one card
  per activity (verdict, why it is not good, best slot, 24 hourly bars with the night dark and a white line
  for now, the main criteria), a legend, the tide curve with the next extremes and the estimated
  coefficient, the sun. Touching a card opens the criteria behind its verdict, each with its value,
  its points when it gives some, and what the rule made of it; touching anywhere closes it.
- **No verdict without data.** Data some minutes old is shown dimmed with its age; data too old, or
  none, shows no verdict at all, only that it is missing. A coastal warning that could not be read is
  said so and is never an all-clear. A criterion whose value is unknown is shown as unknown, never as 0.
  The tide curve is drawn only between the extremes the frame gives, as a half cosine; nothing
  before the first or after the last.
- **The spot is a build setting** (`vigie_spot`, empty by default: no sea page). A spot the API
  refuses (404 or 422) is given up after the first refusal, so the weather keeps coming. A setting
  on the device is in the roadmap, not here.
- **Times** are shown in the clock of the zone on screen: the frame carries no offset for the spot.
- **Every verdict says it is Vigie's own rule**, not an official one, not for safety
  (`SEA_RULES_NOTICE`, on the page and in the detail).
- **The 1 s repaint moved** to its own package (`vigie-repaint.yaml`): the off-device render
  includes the screen package and sets its own clock, which a repaint with the machine's clock
  would undo.
- **The render is driven by its environment** (`VIGIE_RENDER_*`, `vigie-render.yaml`), so that one
  build per board gives every picture: world, sea, detail, warning, data some minutes old, data too old.

## Consequences

- The world page is the same as before; only the title bar gained the pill when a sea exists.
- A sea shown as an all-clear is impossible by construction: every state without data has its own text.
- Touch and swipe are not exercised by the render; they are to be tried on a board.
- The hours are from the zone's clock, so a spot in another time zone than the zone on screen reads
  wrong by the difference until the frame carries the spot's offset.
