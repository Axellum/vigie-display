/**
 * @file panel_power.cpp
 * @brief Screen off = panel really off.
 *
 * A black full-screen veil is drawn just before LVGL pauses, then the panel receives, if
 * it supports them, the DCS commands "display off" (0x28) and "sleep in" (0x10). Back on:
 * "sleep out" (0x11), 120 ms, "display on" (0x29), veil removed, LVGL redraws everything.
 *
 * Why: on the Waveshare 7B the backlight is dimmed analogically (GPIO32 → RC filter →
 * AP3032 feedback) and never goes fully dark: the frozen image stayed visible at 0 %
 * (seen on 2026-10-16). A still image held for hours also marks an IPS panel. The veil
 * blocks the remaining light; the DCS commands really stop the panel.
 * Modes (`VIGIE_PANEL_OFF_MODE`): 0 = veil only; 1 = veil + 0x28; 2 = veil + 0x28 + 0x10.
 * EK79007 (7B): 2, tested on the board on 2026-10-17 (touch is a separate GT911).
 * DCS timings (120 ms between sleep in/out, 120 ms after sleep out before display on): the
 * first two are held here (blocking wait of the remainder, at most 120 ms), the third by
 * the `delay` of the `panel_wake_up` script.
 * ESPHome 2026.9.1 sends no command after initialisation and `mipi_dsi::MipiDsi` is
 * `final` with a protected command channel (`io_handle_`): it is read through an explicit
 * template instantiation (access checks do not apply there, [temp.spec]), no copy or patch
 * of the component. If `io_handle_` changes name or type, the build fails here.
 * The veil is clickable so that it absorbs touches during an OTA (LVGL still runs under
 * the dark panel). Taken from M5-Tab5-ESPHome-LVGL (Tab5/ecran/tab5_dalle.cpp).
 */
#include "panel_power.h"

#include "esp_lcd_panel_io.h"
#include "esphome/components/mipi_dsi/mipi_dsi.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "lvgl.h"

#ifndef VIGIE_PANEL_OFF_MODE
#define VIGIE_PANEL_OFF_MODE 0
#endif
#ifndef VIGIE_PANEL_DARK_DURING_OTA
#define VIGIE_PANEL_DARK_DURING_OTA 0
#endif

namespace {

constexpr const char* TAG = "vigie.panel";
constexpr int kMode = VIGIE_PANEL_OFF_MODE;
static_assert(kMode >= 0 && kMode <= 2, "board_panel_off_mode: 0, 1 or 2");
constexpr uint8_t kDcsSleepIn = 0x10;
constexpr uint8_t kDcsSleepOut = 0x11;
constexpr uint8_t kDcsDisplayOff = 0x28;
constexpr uint8_t kDcsDisplayOn = 0x29;
constexpr uint32_t kSleepDelayMs = 120;

lv_obj_t* s_veil = nullptr;
bool s_display_off = false;  // 0x28 sent, no 0x29 yet
bool s_asleep = false;       // 0x10 sent, no 0x11 yet
uint32_t s_sleep_change_ms = 0;

// Access to MipiDsi::io_handle_ (see the file header).
struct DsiChannel {
    using type = esp_lcd_panel_io_handle_t esphome::mipi_dsi::MipiDsi::*;
    friend type dsi_channel(DsiChannel);
};
template <typename Key, typename Key::type Member>
struct ReadChannel {
    friend typename Key::type dsi_channel(Key) { return Member; }
};
template struct ReadChannel<DsiChannel, &esphome::mipi_dsi::MipiDsi::io_handle_>;

bool send(esphome::mipi_dsi::MipiDsi* panel, uint8_t cmd) {
    if (panel == nullptr) return false;
    esp_lcd_panel_io_handle_t io = panel->*dsi_channel(DsiChannel{});
    if (io == nullptr) return false;  // panel not initialised yet
    const esp_err_t err = esp_lcd_panel_io_tx_param(io, cmd, nullptr, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Command 0x%02X refused (%s)", cmd, esp_err_to_name(err));
        return false;
    }
    return true;
}

// At least 120 ms between two sleep changes (0x10 <-> 0x11): waits for the remainder.
void wait_sleep_delay() {
    const uint32_t elapsed = esphome::millis() - s_sleep_change_ms;
    if (elapsed < kSleepDelayMs) esphome::delay(kSleepDelayMs - elapsed);
}

lv_obj_t* veil() {
    if (s_veil != nullptr) return s_veil;
    s_veil = lv_obj_create(lv_layer_sys());
    lv_obj_remove_style_all(s_veil);
    lv_obj_set_size(s_veil, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_veil, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_veil, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_veil, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_veil, LV_OBJ_FLAG_HIDDEN);
    return s_veil;
}

}  // namespace

void panel_off(esphome::mipi_dsi::MipiDsi* panel) {
    if (lv_display_get_default() != nullptr) {
        lv_obj_remove_flag(veil(), LV_OBJ_FLAG_HIDDEN);
        lv_refr_now(nullptr);  // drawn before lvgl.pause freezes the display
    }
    if (kMode >= 1 && !s_display_off) s_display_off = send(panel, kDcsDisplayOff);
    if (kMode >= 2 && !s_asleep) {
        wait_sleep_delay();
        if (send(panel, kDcsSleepIn)) {
            s_asleep = true;
            s_sleep_change_ms = esphome::millis();
        }
    }
    ESP_LOGI(TAG, "Panel off (mode %d)", kMode);
}

bool panel_wake(esphome::mipi_dsi::MipiDsi* panel) {
    if (!s_asleep) return false;
    wait_sleep_delay();
    // Even if refused, the panel is considered awake, so as not to stay black forever on a
    // transmission error (the log says so).
    send(panel, kDcsSleepOut);
    s_asleep = false;
    s_sleep_change_ms = esphome::millis();
    return true;
}

void panel_on(esphome::mipi_dsi::MipiDsi* panel) {
    // Put back to sleep while the script waited, without the light knowing: should not
    // happen, but a sleeping panel under a lit backlight would stay black until reboot.
    if (s_asleep && panel_wake(panel)) esphome::delay(kSleepDelayMs);
    if (s_display_off) {
        wait_sleep_delay();  // 120 ms after 0x11, even if the script was restarted meanwhile
        send(panel, kDcsDisplayOn);
        s_display_off = false;
    }
    if (s_veil == nullptr) return;  // never turned off since boot
    lv_obj_add_flag(s_veil, LV_OBJ_FLAG_HIDDEN);
    ESP_LOGI(TAG, "Panel on");
}

bool panel_dark_during_ota() { return VIGIE_PANEL_DARK_DURING_OTA != 0; }
