// @file board_7b_version.h
// @brief ESPHome floor of the Waveshare ESP32-P4 7B: 2026.9.1.
//
// ESPHome 2026.9.0 crashes when it starts the display of a P4 v3 ("legacy" DSI clock,
// esphome#18984, fixed in 2026.9.1): the board reboots in a loop, then enters safe mode.
// Checked at compile time from the version ESPHome writes into the build, so that a
// `min_version:` overridden by a package merge cannot hide it. Listed only by
// firmware/packages/board-waveshare-7b.yaml. Taken from M5-Tab5-ESPHome-LVGL
// (Tab5/ecran/carte_7b_version.h).
#pragma once

#include "esphome/core/macros.h"
#include "esphome/core/version.h"

static_assert(ESPHOME_VERSION_CODE >= VERSION_CODE(2026, 9, 1),
              "Waveshare 7B: ESPHome 2026.9.1 or newer is required (2026.9.0 crashes at display start on "
              "a P4 v3, esphome#18984)");
