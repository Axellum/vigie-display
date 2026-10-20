/**
 * @file c6_link.cpp
 * @brief Every software restart of the P4 becomes a reset of the whole digital core (CORE
 *        level), so that the SDIO link to the ESP32-C6 comes back; failures of that link
 *        are logged.
 *
 * Why (measured on the Waveshare 7B on 2026-10-16, serial capture): after a few software
 * restarts, a boot sees the SDIO link fail (read timeouts, 0x107); the P4 restarts with
 * esp_restart(), and every following boot fails the same way until safe mode.
 * esp_restart() only resets the P4's CPUs (rst:0xc): the faulty state survives it. A reset
 * through USB (CORE level) unblocked the board at once, without touching the C6. Hence
 * esp_rom_software_reset_system() instead, for every software restart (button, OTA,
 * safe_mode, ESP-Hosted); ESP-IDF still reports it as a software restart. Panics do not go
 * through here and keep the CPU reset. 20 restarts in a row passed with this file.
 *
 * The shutdown handler is registered in a constructor, hence among the first: esp_restart()
 * calls them from the last registered to the first, so the others (Wi-Fi stop, ESPHome) run
 * before it. ESP-Hosted's `_h_event_post` (table `g_hosted_osi_funcs`, public header of the
 * component) is wrapped to log a transport failure and the C6's last reset reason.
 * Messages written just before a reset use esp_rom_printf: an ESP_LOG* outside the main
 * task waits for loop() and would be lost.
 *
 * Do not go back to a bare esp_restart() without repeating the test (restarts in a row,
 * serial capture). Taken from M5-Tab5-ESPHome-LVGL (Tab5/ecran/tab5_lien_c6.cpp).
 */
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "c6_link.h"

extern "C" {
#include "esp_hosted_os_abstraction.h"
#include "esp_hosted_event.h"
}

// Last: redefines ESP_LOG* to ESPHome's logger (ESP-IDF's are capped at ERROR here).
#include "esphome/core/log.h"

namespace {

const char* const TAG = "vigie.c6";

bool s_core_reset_registered = false;
int (*s_original_event_post)(esp_event_base_t, int32_t, void*, size_t, uint32_t) = nullptr;

// Last shutdown handler called by esp_restart(): CORE reset instead of a CPU reset.
void reset_whole_core() { esp_rom_software_reset_system(); }

int on_hosted_event(esp_event_base_t base, int32_t id, void* data, size_t size, uint32_t wait) {
    if (base == ESP_HOSTED_EVENT) {
        if (id == ESP_HOSTED_EVENT_TRANSPORT_FAILURE) {
            esp_rom_printf("\n[vigie.c6] SDIO transport failure reported by ESP-Hosted\n");
        } else if (id == ESP_HOSTED_EVENT_CP_INIT && data != nullptr) {
            const auto* init = static_cast<const esp_hosted_event_init_t*>(data);
            ESP_LOGI(TAG, "C6 ready, reason of its last reset: %d", (int) init->reason);
        }
    }
    return s_original_event_post ? s_original_event_post(base, id, data, size, wait) : 0;
}

// Before app_main: nothing but two memory writes.
__attribute__((constructor)) void register_c6_link() {
    s_core_reset_registered = esp_register_shutdown_handler(reset_whole_core) == ESP_OK;
    s_original_event_post = g_hosted_osi_funcs._h_event_post;
    g_hosted_osi_funcs._h_event_post = on_hosted_event;
}

}  // namespace

bool c6_link_setup() {
    if (!s_core_reset_registered) {
        ESP_LOGE(TAG, "CORE reset not registered: a software restart will only reset the CPUs");
    }
    return s_core_reset_registered;
}
