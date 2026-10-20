/**
 * @file panel_power.h
 * @brief Screen off = panel really off (black veil + DCS "display off" / "sleep in"), and a
 *        dark panel during a firmware update (see panel_power.cpp).
 *
 * Called only by the `backlight` light, the `panel_wake_up` script and `ota:` of the board
 * package. Per board: `VIGIE_PANEL_OFF_MODE` (substitution `board_panel_off_mode`) and
 * `VIGIE_PANEL_DARK_DURING_OTA` (`board_panel_dark_during_ota`), passed as build flags.
 */
#pragma once

namespace esphome {
namespace mipi_dsi {
class MipiDsi;
}
}  // namespace esphome

// Off: black veil drawn at once, then, depending on the panel, "display off" (0x28) and
// "sleep in" (0x10). Called just before `lvgl.pause`.
void panel_off(esphome::mipi_dsi::MipiDsi* panel);
// Back on, first step: "sleep out" (0x11) if the panel sleeps. True = sent: wait 120 ms
// before panel_on() (the `panel_wake_up` script does).
bool panel_wake(esphome::mipi_dsi::MipiDsi* panel);
// Back on, second step: "display on" (0x29) if needed, veil removed. Called just before
// `lvgl.resume`.
void panel_on(esphome::mipi_dsi::MipiDsi* panel);
// True when the board also darkens its panel during a firmware update.
bool panel_dark_during_ota();
