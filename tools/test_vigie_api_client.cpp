// PC tests of firmware/core/vigie_api_client.cpp against a real frame of vigie-api
// (tests/fixtures/api_frame.txt, path given as argv[1]). Built and run by
// tests/test_firmware.py and the CI.
#include <cstdio>
#include <cstring>

#include "test_check.h"
#include "vigie_api_client.h"
#include "vigie_text.h"

namespace {

char g_frame[32768];

size_t read_file(const char* path) {
    std::FILE* f = std::fopen(path, "rb");
    if (f == nullptr) return 0;
    const size_t n = std::fread(g_frame, 1, sizeof(g_frame) - 1, f);
    std::fclose(f);
    g_frame[n] = '\0';
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

}  // namespace

int main(int argc, char** argv) {
    test_url();
    test_status_sentences();
    const char* path = argc > 1 ? argv[1] : "tests/fixtures/api_frame.txt";
    const size_t size = read_file(path);
    CHECK(size > 0);
    if (size > 0) test_real_frame(size);
    return test_report("vigie_api_client");
}
