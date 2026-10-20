/**
 * @file vigie_api_client.h
 * @brief The display's side of the contract with vigie-api.
 *
 * - URL of the embedded frame (`/v1/embedded/frame`), with the GPS position when a fix
 *   is valid.
 * - The HTTP response: refused unless complete (status 200, not filling the buffer, a
 *   final `END|<CRC32>` line, a matching CRC), else decoded into the world state while
 *   keeping the zone the user was looking at.
 * - The link state (waiting, up to date, down) and its sentence for the status bar.
 *
 * Constraints: C++17, no allocation, no ESPHome nor LVGL; tested on a PC by
 * tools/test_vigie_api_client.cpp against a real frame (tests/fixtures/api_frame.txt).
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "vigie_model.h"

// Older data than this (three missed 60 s refreshes): the link is down.
constexpr uint32_t kVigieStaleAfterMs = 180000;

struct VigieLinkStatus {
    bool online = false;          // last request succeeded
    bool from_cache_only = false;  // state restored from NVS, nothing received since boot
    uint32_t last_success_ms = 0;
    uint32_t success_count = 0;
    uint32_t failure_count = 0;
};

enum class VigieLinkState : uint8_t {
    WAITING = 0,     // nothing received or failed since boot
    UP_TO_DATE = 1,  // last request succeeded and the data is recent
    DOWN = 2,        // last request failed, or the data is stale
};

// "<base_url>/v1/embedded/frame?device_type=<device>" plus "&lat=…&lon=…&marine=1" with a
// valid GPS fix. A trailing '/' on `base_url` is ignored.
bool vigie_build_frame_url(const char* base_url, const char* device_type, const VigieGpsFix& gps, char* buf,
                           size_t size);

VigieLinkState vigie_link_state(const VigieLinkStatus& status, uint32_t now_ms);

// Status bar sentence: "Waiting for the API", "Up to date · 12 s ago",
// "API unreachable · data from 6 min ago" (current language).
bool vigie_format_link_status(const VigieLinkStatus& status, uint32_t now_ms, char* buf, size_t size);

// True when the last non-empty line is "END|" followed by 8 hex digits. A response cut on
// the way (full buffer, Wi-Fi) has none. The CRC itself is checked by vigie_decode_frame().
bool vigie_frame_complete(const char* frame);

// Firmware path. Refuses the response (failure counted, `state` untouched) when the status
// is not 200, when it filled the buffer (`size >= max_size`: maybe cut), when it is not
// complete or its CRC is wrong. Otherwise decodes it, keeps the active zone (same id) and
// evaluates the alarm.
bool vigie_receive_http_response(int http_status, const char* body, size_t size, size_t max_size,
                                 VigieWorldState& state, VigieLinkStatus& status, uint32_t now_ms,
                                 VigieAlarmDecision& alarm);

// Request failed (network, timeout): the link goes down.
void vigie_report_failure(VigieLinkStatus& status);

// Current UTC: `clock_utc` when the clock is set (non-zero, e.g. SNTP), else the server time
// of the last frame plus the time since it arrived; 0 when neither is known.
uint32_t vigie_current_utc(uint32_t clock_utc, const VigieWorldState& state, const VigieLinkStatus& status,
                           uint32_t now_ms);

extern VigieLinkStatus g_vigie_link;
