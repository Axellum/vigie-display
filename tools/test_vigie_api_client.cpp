// PC tests of firmware/core/vigie_api_client.cpp against real frames of vigie-api
// (tests/fixtures/api_frame.txt as argv[1]; tests/fixtures/api_frame_activities.txt, a frame
// with the SEA / ACT / CRIT lines of a surf spot, as argv[2]). Built and run by
// tests/test_firmware.py and the CI.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "test_check.h"
#include "vigie_api_client.h"
#include "vigie_text.h"

namespace {

constexpr size_t kFrameMax = 32768;
char g_frame[kFrameMax];

size_t read_file(const char* path, char* out, size_t out_size) {
    std::FILE* f = std::fopen(path, "rb");
    if (f == nullptr) return 0;
    const size_t n = std::fread(out, 1, out_size - 1, f);
    std::fclose(f);
    out[n] = '\0';
    return n;
}

void test_url() {
    char url[256];
    VigieGpsFix gps{};
    CHECK(vigie_build_frame_url("http://192.168.0.155:8080/", "waveshare-7b", gps, url, sizeof(url)));
    CHECK_STR(url, "http://192.168.0.155:8080/v1/embedded/frame?device_type=waveshare-7b");
    gps.valid = true;
    gps.lat_deg = 48.8565;
    gps.lon_deg = 2.3522;
    CHECK(vigie_build_frame_url("http://h", "tab5", gps, url, sizeof(url)));
    CHECK(std::strstr(url, "&lat=48.85650&lon=2.35220&marine=1") != nullptr);
    CHECK(!vigie_build_frame_url("", "tab5", gps, url, sizeof(url)));
}

void test_real_frame(size_t size) {
    CHECK(vigie_frame_complete(g_frame));
    VigieWorldState s{};
    VigieLinkStatus link{};
    VigieAlarmDecision alarm{};

    // Refusals: wrong status, buffer full (maybe cut), cut frame. The state stays empty.
    CHECK(!vigie_receive_http_response(503, g_frame, size, sizeof(g_frame), s, link, 1000, alarm));
    CHECK(!vigie_receive_http_response(200, g_frame, size, size, s, link, 1000, alarm));
    static char cut[sizeof(g_frame)];
    std::memcpy(cut, g_frame, size / 2);  // the first half: what a dropped connection leaves
    cut[size / 2] = '\0';
    CHECK(!vigie_frame_complete(cut));
    CHECK(!vigie_receive_http_response(200, cut, std::strlen(cut), sizeof(g_frame), s, link, 1000, alarm));
    CHECK(s.zone_count == 0 && link.failure_count == 3 && !link.online);
    CHECK(vigie_link_state(link, 1000) == VigieLinkState::DOWN);

    // Accepted: zones and hazards decoded.
    CHECK(vigie_receive_http_response(200, g_frame, size, sizeof(g_frame), s, link, 2000, alarm));
    CHECK(link.online && link.success_count == 1);
    CHECK(vigie_link_state(link, 2000 + 5000) == VigieLinkState::UP_TO_DATE);
    CHECK(vigie_link_state(link, 2000 + kVigieStaleAfterMs + 1) == VigieLinkState::DOWN);
    CHECK(s.zone_count >= 1 && s.updated_utc > 1700000000u);
    for (uint8_t i = 0; i < s.zone_count; ++i) {
        CHECK(s.zones[i].id[0] != '\0' && s.zones[i].name[0] != '\0');
        CHECK(s.zones[i].day_count >= 1);
        // Day labels are ISO dates the display turns into day names.
        CHECK(std::strlen(s.zones[i].days[0].label) == 10 && s.zones[i].days[0].label[4] == '-');
    }
    for (uint8_t i = 1; i < s.hazard_count; ++i) {
        CHECK(static_cast<uint8_t>(s.hazards[i - 1].severity) >= static_cast<uint8_t>(s.hazards[i].severity));
    }
    // HEAD counts every active hazard: at least the HAZ lines received.
    CHECK(s.hazards_known);
    uint16_t sent = 0;
    for (uint8_t i = 0; i < s.hazard_count; ++i) sent += s.hazards[i].severity != VigieSeverity::GREEN ? 1 : 0;
    CHECK(s.red_count + s.orange_count + s.yellow_count >= sent);

    // Time without a set clock: the frame's server time plus the time since it arrived.
    CHECK(vigie_current_utc(1800000000u, s, link, 9000) == 1800000000u);
    CHECK(vigie_current_utc(0, s, link, 2000 + 61000) == s.updated_utc + 61);
    CHECK(vigie_current_utc(0, VigieWorldState{}, VigieLinkStatus{}, 5000) == 0);

    // The zone on screen survives the next frame (which says zone 0).
    if (s.zone_count >= 2) {
        vigie_next_zone(s);
        char shown[kVigieIdMax];
        std::strcpy(shown, s.zones[s.active_zone].id);
        CHECK(vigie_receive_http_response(200, g_frame, size, sizeof(g_frame), s, link, 3000, alarm));
        CHECK_STR(s.zones[s.active_zone].id, shown);
    }

    // A flipped byte in the body: CRC refused, state kept.
    const uint8_t zones_before = s.zone_count;
    char* p = std::strstr(g_frame, "ZONE|");
    CHECK(p != nullptr);
    if (p != nullptr) {
        p[5] = p[5] == 'x' ? 'y' : 'x';
        CHECK(!vigie_receive_http_response(200, g_frame, size, sizeof(g_frame), s, link, 4000, alarm));
        CHECK(s.zone_count == zones_before);
    }
}

void test_status_sentences() {
    char buf[96];
    VigieLinkStatus link{};
    vigie_set_language(VigieLanguage::EN);
    CHECK(vigie_format_link_status(link, 0, buf, sizeof(buf)));
    CHECK_STR(buf, "Waiting for the API");
    link.online = true;
    link.success_count = 1;
    link.last_success_ms = 1000;
    CHECK(vigie_format_link_status(link, 13000, buf, sizeof(buf)));
    CHECK_STR(buf, "Up to date \xC2\xB7 12 s ago");
    vigie_report_failure(link);
    CHECK(vigie_format_link_status(link, 1000 + 6 * 60000, buf, sizeof(buf)));
    CHECK_STR(buf, "API unreachable \xC2\xB7 data from 6 min ago");
    vigie_set_language(VigieLanguage::FR);
    CHECK(vigie_format_link_status(link, 1000 + 6 * 60000, buf, sizeof(buf)));
    CHECK_STR(buf, "API injoignable \xC2\xB7 données d'il y a 6 min");
    vigie_set_language(VigieLanguage::EN);
}

// ─── A frame with the activities block (SEA, ACT, CRIT) ─────────────────────
// vigie-api sends these lines for a surf spot; this firmware does not read them yet. It must
// accept the frame and decode everything else exactly as before.

// Number of lines of `frame` that start with `tag` (e.g. "ACT|").
int count_lines(const char* frame, const char* tag) {
    const size_t len = std::strlen(tag);
    int count = 0;
    for (const char* line = frame; line != nullptr && *line != '\0';) {
        if (std::strncmp(line, tag, len) == 0) ++count;
        line = std::strchr(line, '\n');
        if (line != nullptr) ++line;
    }
    return count;
}

// `frame` without its SEA / ACT / CRIT lines, signed again: the frame as it was before the
// activities block existed. Returns the new size, 0 if `out` is too small.
size_t strip_activities(const char* frame, char* out, size_t out_size) {
    size_t n = 0;
    for (const char* line = frame; *line != '\0';) {
        const char* newline = std::strchr(line, '\n');
        const size_t len = newline != nullptr ? static_cast<size_t>(newline - line) + 1 : std::strlen(line);
        if (std::strncmp(line, "END|", 4) == 0) break;
        const bool activities = std::strncmp(line, "SEA|", 4) == 0 || std::strncmp(line, "ACT|", 4) == 0 ||
                                std::strncmp(line, "CRIT|", 5) == 0;
        if (!activities) {
            if (n + len >= out_size) return 0;
            std::memcpy(out + n, line, len);
            n += len;
        }
        line += len;
    }
    if (n + 14 >= out_size) return 0;
    const int tail = std::snprintf(out + n, out_size - n, "END|%08X\n", static_cast<unsigned>(vigie_crc32(out, n)));
    return tail > 0 ? n + static_cast<size_t>(tail) : 0;
}

bool same_float(float a, float b) { return (std::isnan(a) && std::isnan(b)) || a == b; }

// Same decoded content (the fields the screen reads), unknown values included.
bool same_world(const VigieWorldState& a, const VigieWorldState& b) {
    if (a.updated_utc != b.updated_utc || a.hazards_known != b.hazards_known || a.zone_count != b.zone_count ||
        a.hazard_count != b.hazard_count || a.red_count != b.red_count || a.orange_count != b.orange_count ||
        a.yellow_count != b.yellow_count || a.green_count != b.green_count || !same_float(a.kp, b.kp)) {
        return false;
    }
    for (uint8_t i = 0; i < a.zone_count; ++i) {
        const VigieZone& x = a.zones[i];
        const VigieZone& y = b.zones[i];
        if (std::strcmp(x.id, y.id) != 0 || std::strcmp(x.name, y.name) != 0 || x.has_weather != y.has_weather ||
            x.utc_offset_s != y.utc_offset_s || x.utc_offset_known != y.utc_offset_known || x.lat != y.lat ||
            x.lon != y.lon || !same_float(x.temp_c, y.temp_c) || !same_float(x.wind_kmh, y.wind_kmh) ||
            !same_float(x.gust_kmh, y.gust_kmh) || x.wind_deg != y.wind_deg || x.weather_code != y.weather_code ||
            x.aqi != y.aqi || x.marine.valid != y.marine.valid ||
            !same_float(x.marine.wave_height_m, y.marine.wave_height_m) || x.day_count != y.day_count) {
            return false;
        }
        for (uint8_t d = 0; d < x.day_count; ++d) {
            if (std::strcmp(x.days[d].label, y.days[d].label) != 0 || x.days[d].weather_code != y.days[d].weather_code ||
                !same_float(x.days[d].tmin_c, y.days[d].tmin_c) || !same_float(x.days[d].tmax_c, y.days[d].tmax_c)) {
                return false;
            }
        }
    }
    for (uint8_t i = 0; i < a.hazard_count; ++i) {
        if (std::strcmp(a.hazards[i].id, b.hazards[i].id) != 0 || a.hazards[i].severity != b.hazards[i].severity ||
            !same_float(a.hazards[i].distance_km, b.hazards[i].distance_km)) {
            return false;
        }
    }
    return true;
}

void test_activities_lines_are_skipped(size_t size) {
    // The fixture is a real frame with the whole block: one SEA, four activities, their criteria.
    CHECK(vigie_frame_complete(g_frame));
    CHECK(vigie_frame_crc_ok(g_frame));
    CHECK(count_lines(g_frame, "SEA|") == 1);
    CHECK(count_lines(g_frame, "ACT|") == 4);
    CHECK(count_lines(g_frame, "CRIT|") >= 4);
    // Each line fits the 512-byte buffer of the contract, and the frame the device buffer.
    CHECK(size < 8192);

    VigieWorldState with_block{};
    VigieLinkStatus link{};
    VigieAlarmDecision alarm{};
    CHECK(vigie_receive_http_response(200, g_frame, size, sizeof(g_frame), with_block, link, 1000, alarm));
    CHECK(link.online && link.failure_count == 0);
    CHECK(with_block.zone_count >= 1 && with_block.updated_utc > 1700000000u);

    // The same frame without the block, signed again, gives the same world.
    static char stripped[kFrameMax];
    const size_t stripped_size = strip_activities(g_frame, stripped, sizeof(stripped));
    CHECK(stripped_size > 0 && stripped_size < size);
    CHECK(count_lines(stripped, "SEA|") + count_lines(stripped, "ACT|") + count_lines(stripped, "CRIT|") == 0);
    CHECK(vigie_frame_complete(stripped) && vigie_frame_crc_ok(stripped));
    VigieWorldState without_block{};
    VigieLinkStatus other_link{};
    VigieAlarmDecision other_alarm{};
    CHECK(vigie_receive_http_response(200, stripped, stripped_size, sizeof(stripped), without_block, other_link, 1000,
                                      other_alarm));
    CHECK(same_world(with_block, without_block));
    CHECK(alarm.trigger == other_alarm.trigger);

    // A line of the block altered after signing is still refused: the CRC covers it.
    char* act = std::strstr(g_frame, "ACT|");
    CHECK(act != nullptr);
    if (act != nullptr) {
        act[4] = act[4] == 'x' ? 'y' : 'x';
        VigieWorldState untouched{};
        CHECK(!vigie_receive_http_response(200, g_frame, size, sizeof(g_frame), untouched, link, 2000, alarm));
        CHECK(untouched.zone_count == 0);
    }
}

}  // namespace

int main(int argc, char** argv) {
    test_url();
    test_status_sentences();
    const char* path = argc > 1 ? argv[1] : "tests/fixtures/api_frame.txt";
    const size_t size = read_file(path, g_frame, sizeof(g_frame));
    CHECK(size > 0);
    if (size > 0) test_real_frame(size);
    const char* activities_path = argc > 2 ? argv[2] : "tests/fixtures/api_frame_activities.txt";
    const size_t activities_size = read_file(activities_path, g_frame, sizeof(g_frame));
    CHECK(activities_size > 0);
    if (activities_size > 0) test_activities_lines_are_skipped(activities_size);
    return test_report("vigie_api_client");
}
