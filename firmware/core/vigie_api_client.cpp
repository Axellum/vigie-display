/**
 * @file vigie_api_client.cpp
 * @brief Implementation of vigie_api_client.h (see the header).
 */
#include "vigie_api_client.h"

#include <cstdio>
#include <cstring>

#include "vigie_text.h"

VigieLinkStatus g_vigie_link{};

namespace {

// Age of the data in seconds (millis() wraps after 49 days: unsigned difference).
uint32_t data_age_s(const VigieLinkStatus& status, uint32_t now_ms) { return (now_ms - status.last_success_ms) / 1000UL; }

bool format_short_age(uint32_t age_s, char* buf, size_t size) {
    int n = 0;
    if (age_s < 60UL) {
        n = std::snprintf(buf, size, "%lu s", static_cast<unsigned long>(age_s));
    } else if (age_s < 3600UL) {
        n = std::snprintf(buf, size, "%lu min", static_cast<unsigned long>(age_s / 60UL));
    } else {
        n = std::snprintf(buf, size, "%lu h", static_cast<unsigned long>(age_s / 3600UL));
    }
    return n > 0 && static_cast<size_t>(n) < size;
}

}  // namespace

bool vigie_build_frame_url(const char* base_url, const char* device_type, const VigieGpsFix& gps, char* buf,
                           size_t size) {
    if (buf == nullptr || size < 32 || base_url == nullptr || base_url[0] == '\0') {
        if (buf != nullptr && size > 0) buf[0] = '\0';
        return false;
    }
    const char* device = (device_type != nullptr && device_type[0] != '\0') ? device_type : "waveshare-7b";
    size_t base_len = std::strlen(base_url);
    while (base_len > 0 && base_url[base_len - 1] == '/') --base_len;
    int n = 0;
    if (gps.valid) {
        n = std::snprintf(buf, size, "%.*s/v1/embedded/frame?device_type=%s&lat=%.5f&lon=%.5f&marine=1",
                          static_cast<int>(base_len), base_url, device, gps.lat_deg, gps.lon_deg);
    } else {
        n = std::snprintf(buf, size, "%.*s/v1/embedded/frame?device_type=%s", static_cast<int>(base_len), base_url,
                          device);
    }
    return n > 0 && static_cast<size_t>(n) < size;
}

VigieLinkState vigie_link_state(const VigieLinkStatus& status, uint32_t now_ms) {
    if (status.success_count == 0 && !status.from_cache_only) {
        return status.failure_count == 0 ? VigieLinkState::WAITING : VigieLinkState::DOWN;
    }
    if (!status.online || status.success_count == 0) return VigieLinkState::DOWN;
    if (now_ms - status.last_success_ms > kVigieStaleAfterMs) return VigieLinkState::DOWN;
    return VigieLinkState::UP_TO_DATE;
}

bool vigie_format_link_status(const VigieLinkStatus& status, uint32_t now_ms, char* buf, size_t size) {
    if (buf == nullptr || size < 16) return false;
    buf[0] = '\0';
    char age[16];
    int n = 0;
    switch (vigie_link_state(status, now_ms)) {
        case VigieLinkState::WAITING:
            n = std::snprintf(buf, size, "%s", vigie_tr(VigieText::LINK_WAITING));
            break;
        case VigieLinkState::UP_TO_DATE:
            if (!format_short_age(data_age_s(status, now_ms), age, sizeof(age))) return false;
            n = std::snprintf(buf, size, vigie_tr(VigieText::LINK_UP_TO_DATE), age);
            break;
        case VigieLinkState::DOWN:
            if (status.success_count == 0) {
                n = std::snprintf(buf, size, "%s", vigie_tr(VigieText::LINK_DOWN));
            } else {
                if (!format_short_age(data_age_s(status, now_ms), age, sizeof(age))) return false;
                n = std::snprintf(buf, size, vigie_tr(VigieText::LINK_DOWN_SINCE), age);
            }
            break;
    }
    return n > 0 && static_cast<size_t>(n) < size;
}

bool vigie_frame_complete(const char* frame) {
    if (frame == nullptr) return false;
    const char* p = frame + std::strlen(frame);
    while (p > frame && (p[-1] == '\n' || p[-1] == '\r' || p[-1] == ' ' || p[-1] == '\t')) --p;
    const char* end = p;
    while (p > frame && p[-1] != '\n') --p;
    if (end - p != 12 || std::strncmp(p, "END|", 4) != 0) return false;
    for (const char* c = p + 4; c < end; ++c) {
        const bool hex = (*c >= '0' && *c <= '9') || (*c >= 'A' && *c <= 'F') || (*c >= 'a' && *c <= 'f');
        if (!hex) return false;
    }
    return true;
}

void vigie_report_failure(VigieLinkStatus& status) {
    status.online = false;
    ++status.failure_count;
}

uint32_t vigie_current_utc(uint32_t clock_utc, const VigieWorldState& state, const VigieLinkStatus& status,
                           uint32_t now_ms) {
    if (clock_utc != 0) return clock_utc;
    if (state.updated_utc == 0 || status.success_count == 0) return 0;
    return state.updated_utc + (now_ms - status.last_success_ms) / 1000UL;
}

bool vigie_receive_http_response(int http_status, const char* body, size_t size, size_t max_size,
                                 VigieWorldState& state, VigieLinkStatus& status, uint32_t now_ms,
                                 VigieAlarmDecision& alarm) {
    alarm = VigieAlarmDecision{};
    if (http_status != 200 || body == nullptr || size == 0 || size >= max_size || !vigie_frame_complete(body)) {
        vigie_report_failure(status);
        return false;
    }

    // The frame resets the active zone to the API's choice: keep the one on screen.
    char shown_zone[kVigieIdMax] = "";
    if (state.active_zone >= 0 && state.active_zone < static_cast<int8_t>(state.zone_count)) {
        std::strncpy(shown_zone, state.zones[state.active_zone].id, sizeof(shown_zone) - 1);
    }

    if (!vigie_decode_frame(body, state)) {  // wrong CRC: `state` was not touched
        vigie_report_failure(status);
        return false;
    }

    if (shown_zone[0] != '\0') {
        for (uint8_t i = 0; i < state.zone_count; ++i) {
            if (std::strcmp(state.zones[i].id, shown_zone) == 0) {
                state.active_zone = static_cast<int8_t>(i);
                break;
            }
        }
    }
    if (state.active_zone < 0 || state.active_zone >= static_cast<int8_t>(state.zone_count)) state.active_zone = 0;
    vigie_update_distances(state);  // from the zone kept on screen

    status.online = true;
    status.from_cache_only = false;
    status.last_success_ms = now_ms;
    ++status.success_count;
    alarm = vigie_evaluate_alarm(state);
    return true;
}
