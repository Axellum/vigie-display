// PC tests of firmware/core/vigie_model.cpp: geometry, NMEA, frame decoding, CRC,
// NVS block, zones and alarm. Built and run by tests/test_firmware.py and the CI.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "test_check.h"
#include "vigie_model.h"

namespace {

void test_geometry() {
    // Brest → Azores: ~2,170 km, towards the west-south-west.
    const double d = vigie_haversine_km(48.39, -4.486, 38.5, -28.0);
    CHECK(d > 2100.0 && d < 2250.0);
    const double b = vigie_bearing_deg(48.39, -4.486, 38.5, -28.0);
    CHECK(b > 240.0 && b < 260.0);
    CHECK_STR(vigie_compass_point(b, false), "WSW");
    CHECK_STR(vigie_compass_point(b, true), "OSO");
    CHECK_STR(vigie_compass_point(-0.1, false), "N");
    CHECK(vigie_haversine_km(NAN, 0, 0, 0) == 0.0);

    char buf[40];
    CHECK(vigie_format_coord_dm(48.8565, 2.352167, buf, sizeof(buf)));
    CHECK_STR(buf, "48\xC2\xB0" "51.39'N 002\xC2\xB0" "21.13'E");
    CHECK(vigie_format_coord_dms(38.1, 142.86, buf, sizeof(buf)));
    CHECK_STR(buf, "38\xC2\xB0" "06'00\"N 142\xC2\xB0" "51'36\"E");
}

void test_nmea() {
    CHECK(vigie_nmea_checksum_ok("$GNGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*59"));
    CHECK(!vigie_nmea_checksum_ok("$GNGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*00"));
    CHECK(!vigie_nmea_checksum_ok("GNGGA without dollar*4B"));

    VigieGpsFix fix{};
    CHECK(vigie_parse_nmea("$GNGGA,142000.00,4851.3900,N,00221.1300,E,1,11,0.8,35.2,M,47.0,M,,*4B", fix));
    CHECK(fix.valid && fix.satellites == 11);
    CHECK(std::fabs(fix.hdop - 0.8f) < 0.01f && std::fabs(fix.altitude_m - 35.2f) < 0.05f);
    CHECK(std::fabs(fix.lat_deg - 48.8565) < 0.0005 && std::fabs(fix.lon_deg - 2.352167) < 0.0005);

    // A corrupted RMC is refused and leaves the fix as it was.
    CHECK(!vigie_parse_nmea("$GNRMC,142530.00,A,4823.4000,N,00429.1600,W,14.2,270.0,181026,,,A*FF", fix));
    CHECK(std::fabs(fix.lat_deg - 48.8565) < 0.0005);
    CHECK(vigie_parse_nmea("$GNRMC,142530.00,A,4823.4000,N,00429.1600,W,14.2,270.0,181026,,,A*6C", fix));
    CHECK(fix.valid && std::fabs(fix.lat_deg - 48.39) < 0.0005 && std::fabs(fix.lon_deg + 4.486) < 0.0005);
    CHECK(std::fabs(fix.speed_knots - 14.2f) < 0.05f && std::fabs(fix.course_deg - 270.0f) < 0.05f);
}

const char* kFrame =
    "HEAD|1792332000|6.3|0|1.1|waveshare-7b|RED|1|1|1|1|1|0\n"
    "ZONE|GPS|Bay of Biscay|48.0|-4.0|7200|18.4|16.9|82|1004.8|45.4|63.0|255|95|4.2|24|15|8.2|0.5,3.2,7.8,2.1|3.4|11.5|280|2.9|1.8\n"
    "ZONE|REYK|Reykjavik|64.14|-21.94|0|6.2|3.0|78|998.0|30.0|45.0|180|61|1.2|12|-1|0.0|0.00,0.00,0.00,0.00|||||\n"
    "ZONE|NODATA|No weather|10.0|10.0|0||||||||||||||||||\n"
    "RAIN5M|GPS|0.0,0.2,0.8,2.4,5.6,8.2,6.4,3.8,1.9,0.6,0.1,0.0\n"
    "DAY|GPS|2026-10-19|61|13.2|19.1|14.5|85|58.0|4.0\n"
    "HAZ|IMO-IS-2026|VOLCANO|ORANGE|63.88|-22.43|4.0|5.0|0|1792331000|IMO|Reykjanes eruption\n"
    "HAZ|USGS-AZORES-01|TSUNAMI|RED|38.5|-28.0|7.4|18.0|1|1792331500|USGS+PTWC|M 7.4 Azores\n"
    "HAZ|EMSC-1|EARTHQUAKE|YELLOW|40.0|20.0|5.1|10.0|0|1792331600|EMSC|M 5.1 Greece\n";

void test_frame_decoding() {
    VigieWorldState s{};
    s.gps.valid = true;  // a moving boat: distances from the fix
    s.gps.lat_deg = 48.39;
    s.gps.lon_deg = -4.486;
    CHECK(vigie_decode_frame(kFrame, s));
    CHECK(s.updated_utc == 1792332000u && std::fabs(s.kp - 6.3f) < 0.01f);
    CHECK(s.solar_level == VigieSolarLevel::G2_MODERATE);
    CHECK(s.zone_count == 3);
    CHECK(s.zones[0].is_mobile_gps && std::fabs(s.zones[0].lat - 48.39) < 0.001);  // follows the fix
    CHECK(s.zones[0].has_weather && s.zones[0].marine.valid);
    CHECK(std::fabs(s.zones[0].marine.wave_height_m - 3.4f) < 0.01f);
    CHECK(s.zones[0].rain.expected && s.zones[0].rain.starts_in_min == 15);
    CHECK(std::fabs(s.zones[0].rain.slots_5m[5] - 8.2f) < 0.01f);
    CHECK(s.zones[0].day_count == 1 && std::strcmp(s.zones[0].days[0].label, "2026-10-19") == 0);
    CHECK(s.zones[0].days[0].rain_probability_pct == 85);
    CHECK(s.zones[0].utc_offset_known && s.zones[0].aqi == 24);
    // Reykjavik: dry for the whole hour (known), no marine data (empty fields).
    CHECK(s.zones[1].rain.known && !s.zones[1].rain.expected && !s.zones[1].marine.valid);
    CHECK(!vigie_known(s.zones[1].marine.wave_period_s));
    CHECK(!s.zones[2].has_weather && !s.zones[2].rain.known && !vigie_known(s.zones[2].temp_c));
    CHECK(s.zones[2].humidity_pct == kVigieUnknownPct && s.zones[2].weather_code == kVigieUnknownCode);

    // Sorted: RED first, then ORANGE, then YELLOW.
    CHECK(s.hazards_known && s.hazard_count == 3);
    CHECK_STR(s.hazards[0].id, "USGS-AZORES-01");
    CHECK(s.hazards[0].type == VigieHazardType::TSUNAMI && s.hazards[0].tsunami_warning);
    CHECK(s.hazards[0].tsunami_eta_min > 100);
    CHECK_STR(s.hazards[1].id, "IMO-IS-2026");
    CHECK(s.red_count == 1 && s.orange_count == 1 && s.yellow_count == 1);

    // Signed frame: accepted; a wrong CRC is refused and leaves the state untouched.
    const char* body =
        "HEAD|1792339999|7.2|0|1.1|waveshare-7b|RED|1|0|0|1|1|0\n"
        "HAZ|NHC-CAT5|HURRICANE|RED|25.0|-70.0|260.0||0|1792339000|NHC|Cat 5\n";
    char good[512], bad[512];
    std::snprintf(good, sizeof(good), "%sEND|%08X\n", body, static_cast<unsigned>(vigie_crc32(body, std::strlen(body))));
    std::snprintf(bad, sizeof(bad), "%sEND|DEADBEEF\n", body);
    CHECK(vigie_frame_crc_ok(good));
    CHECK(!vigie_frame_crc_ok(bad));
    CHECK(!vigie_decode_frame(bad, s));
    CHECK(s.hazard_count == 3 && s.updated_utc == 1792332000u);
    CHECK(vigie_decode_frame(good, s));
    CHECK(s.hazard_count == 1 && s.hazards[0].type == VigieHazardType::CYCLONE);
    CHECK(!vigie_known(s.hazards[0].depth_km));
}

// Empty fields of the contract (vigie-api docs/embedded-frame.md): they stay unknown.
// Oslo comes from the fallback provider (no feels-like, gust, UV, air quality, nowcast, rain
// probability); the GPS zone has no weather and no UTC offset; Kp is unknown; HEAD counts
// every active hazard, more than the HAZ lines sent.
const char* kPartialFrame =
    "HEAD|1792332100||0|1.1|waveshare-7b|RED|2|5|17|||0\n"
    "ZONE|OSLO|Oslo, NO|59.9139|10.7522|7200|3.5||88|998.2|30.2||200|63||||||||||\n"
    "RAIN5M|OSLO|\n"
    "DAY|OSLO|2026-10-20|63|1.0|4.5|3.2||38.0|\n"
    "ZONE|GPS|GPS position|47.75|-3.36|||||||||||||||||||\n"
    "RAIN5M|GPS|\n"
    "HAZ|noaa_1234|TORNADO|RED|35.5|-97.5|||0|1792334300|NOAA_NWS|Tornado Warning\n"
    "HAZ|usgs_1|EARTHQUAKE|RED|35.15|25.2|6.5|18.0|0|1792334500|USGS|M 6.5 Crete\n";

// No hazard source answered the API: severity and counts are empty.
const char* kNoHazardSource = "HEAD|1792332200|2.0|0|1.1|waveshare-7b|||||0|0|0\n";

void test_unknown_fields() {
    VigieWorldState s{};
    s.active_zone = 1;
    CHECK(vigie_decode_frame(kPartialFrame, s));
    CHECK(s.active_zone == 1);  // HEAD field 3 is reserved, not a zone index
    CHECK(!vigie_known(s.kp) && s.solar_level == VigieSolarLevel::QUIET);
    CHECK(s.hazards_known && s.red_count == 2 && s.orange_count == 5 && s.yellow_count == 17);
    CHECK(s.zone_count == 2);

    const VigieZone& oslo = s.zones[0];
    CHECK(oslo.has_weather && oslo.utc_offset_known && oslo.utc_offset_s == 7200);
    CHECK(std::fabs(oslo.temp_c - 3.5f) < 0.01f && !vigie_known(oslo.feels_like_c) && !vigie_known(oslo.gust_kmh));
    CHECK(oslo.humidity_pct == 88 && oslo.wind_deg == 200 && oslo.weather_code == 63);
    CHECK(!vigie_known(oslo.uv) && oslo.aqi == kVigieUnknownCode && !oslo.rain.known && !oslo.marine.valid);
    CHECK(oslo.day_count == 1 && oslo.days[0].rain_probability_pct == kVigieUnknownPct);
    CHECK(std::fabs(oslo.days[0].rain_mm - 3.2f) < 0.01f && !vigie_known(oslo.days[0].uv_max));

    const VigieZone& gps = s.zones[1];
    CHECK(gps.is_mobile_gps && !gps.has_weather && !gps.utc_offset_known);
    CHECK(!vigie_known(gps.temp_c) && gps.weather_code == kVigieUnknownCode && gps.day_count == 0);

    // Same severity: the hazard with a known value first; unknown value and depth stay unknown.
    CHECK(s.hazard_count == 2);
    CHECK_STR(s.hazards[0].id, "usgs_1");
    CHECK(!vigie_known(s.hazards[1].value) && !vigie_known(s.hazards[1].depth_km));

    // No hazard source: not an all-clear, and the zone index is brought back into range.
    CHECK(vigie_decode_frame(kNoHazardSource, s));
    CHECK(!s.hazards_known && s.hazard_count == 0 && s.zone_count == 0 && s.active_zone == 0);
    CHECK(std::fabs(s.kp - 2.0f) < 0.01f);
}

void test_zones_nvs_alarm() {
    VigieWorldState s{};
    CHECK(vigie_decode_frame(kFrame, s));
    CHECK(s.active_zone == 0);
    CHECK(vigie_next_zone(s) == 1);
    CHECK(vigie_next_zone(s) == 2);
    CHECK(vigie_next_zone(s) == 0);
    CHECK(vigie_previous_zone(s) == 2);
    // Without GPS, distances are measured from the active zone.
    vigie_next_zone(s);  // back to 0 then 1 (Reykjavik)
    vigie_next_zone(s);
    CHECK(s.active_zone == 1);
    CHECK(s.hazards[1].distance_km < 50.0f);  // Reykjanes is next to Reykjavik

    VigieNvsBlock block{};
    vigie_nvs_pack(s, block);
    CHECK(block.magic == kVigieNvsMagic && block.version == kVigieNvsVersion);
    VigieWorldState restored{};
    CHECK(vigie_nvs_unpack(block, restored));
    CHECK(restored.hazard_count == 3 && std::strcmp(restored.hazards[0].id, "USGS-AZORES-01") == 0);
    block.crc32 ^= 1u;
    CHECK(!vigie_nvs_unpack(block, restored));

    // RED triggers; once acknowledged, the next critical one (ORANGE within 1,500 km) does.
    VigieAlarmDecision a = vigie_evaluate_alarm(s);
    CHECK(a.trigger && a.wake_panel && a.sound && std::strcmp(a.hazard_id, "USGS-AZORES-01") == 0);
    CHECK(vigie_acknowledge(s, "USGS-AZORES-01") && vigie_is_acknowledged(s, "USGS-AZORES-01"));
    a = vigie_evaluate_alarm(s);
    CHECK(a.trigger && std::strcmp(a.hazard_id, "IMO-IS-2026") == 0);
    CHECK(vigie_acknowledge(s, "IMO-IS-2026"));
    a = vigie_evaluate_alarm(s);
    CHECK(!a.trigger);  // only a YELLOW one is left
    for (int i = 0; i < 40; ++i) {  // the ring keeps the last 16
        char id[16];
        std::snprintf(id, sizeof(id), "X%d", i);
        vigie_acknowledge(s, id);
    }
    CHECK(s.acknowledged_count == kVigieMaxAcknowledged && vigie_is_acknowledged(s, "X39") && !vigie_is_acknowledged(s, "X0"));
}

}  // namespace

int main() {
    test_geometry();
    test_nmea();
    test_frame_decoding();
    test_unknown_fields();
    test_zones_nvs_alarm();
    return test_report("vigie_model");
}
