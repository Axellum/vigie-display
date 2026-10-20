// PC tests of firmware/core/vigie_text.cpp: map projection, labels, numbers, times, in
// English and French. Built and run by tests/test_firmware.py and the CI.
#include <cmath>

#include "test_check.h"
#include "vigie_text.h"

namespace {

constexpr uint32_t kUtc = 1792479349u;  // 2026-10-20 06:55:49 UTC (a real API response)

void test_map() {
    VigiePixel p = vigie_map_project(kVigieMapLatNorth, -180.0, kVigieMapWidth, kVigieMapHeight);
    CHECK(p.x == 0 && p.y == 0 && p.inside);
    p = vigie_map_project(kVigieMapLatSouth, 180.0, kVigieMapWidth, kVigieMapHeight);
    CHECK(p.x == kVigieMapWidth - 1 && p.y == kVigieMapHeight - 1 && p.inside);
    p = vigie_map_project(0.0, 0.0, kVigieMapWidth, kVigieMapHeight);
    CHECK(p.x == 332 && p.y == 155);
    p = vigie_map_project(-75.0, 10.0, kVigieMapWidth, kVigieMapHeight);
    CHECK(!p.inside && p.y == kVigieMapHeight - 1);
    CHECK(!vigie_map_project(std::nan(""), 0.0, kVigieMapWidth, kVigieMapHeight).inside);
}

void test_english() {
    vigie_set_language(VigieLanguage::EN);
    char buf[64];
    CHECK_STR(vigie_weather_label(3), "Overcast");
    CHECK_STR(vigie_weather_label(1234), "Unknown");
    CHECK_STR(vigie_weather_icon(0, true), "\U000F0594");
    CHECK_STR(vigie_weather_icon(0, false), "\U000F0599");
    CHECK_STR(vigie_hazard_name(VigieHazardType::WILDFIRE), "Wildfire");
    CHECK_STR(vigie_tr(VigieText::ACTIVE_ALERTS), "Active alerts");
    CHECK_STR(vigie_aqi_label(47), "Moderate");
    CHECK_STR(vigie_aqi_label(0), "Good");
    CHECK_STR(vigie_aqi_label(kVigieUnknownCode), "");
    CHECK_STR(vigie_weather_label(kVigieUnknownCode), "");
    CHECK_STR(vigie_weather_icon(kVigieUnknownCode, false), "");
    CHECK_STR(vigie_tr(VigieText::ALERTS_UNKNOWN), "Alert sources unreachable");

    CHECK(vigie_format_decimal(6.26f, 1, buf, sizeof(buf)));
    CHECK_STR(buf, "6.3");
    CHECK(vigie_format_decimal(-0.04f, 1, buf, sizeof(buf)));
    CHECK_STR(buf, "0.0");
    CHECK(vigie_format_distance(16873.0f, buf, sizeof(buf)));
    CHECK_STR(buf, "16,900 km");
    CHECK(vigie_format_distance(850.4f, buf, sizeof(buf)));
    CHECK_STR(buf, "850 km");
    CHECK(!vigie_format_distance(-1.0f, buf, sizeof(buf)));

    CHECK(vigie_format_age(10000, 9990, buf, sizeof(buf)));
    CHECK_STR(buf, "just now");
    CHECK(vigie_format_age(10000, 10000 - 25 * 60, buf, sizeof(buf)));
    CHECK_STR(buf, "25 min ago");
    CHECK(vigie_format_age(1000000, 1000000 - 2 * 86400, buf, sizeof(buf)));
    CHECK_STR(buf, "2 d ago");
    CHECK(!vigie_format_age(0, 200, buf, sizeof(buf)));

    CHECK(vigie_format_clock(kUtc, 7200, buf, sizeof(buf)));
    CHECK_STR(buf, "08:55");
    CHECK(vigie_format_clock(kUtc, -14400, buf, sizeof(buf)));
    CHECK_STR(buf, "02:55");
    CHECK(vigie_format_date(kUtc, 7200, buf, sizeof(buf)));
    CHECK_STR(buf, "Tuesday, October 20");
    CHECK(vigie_format_day_short("2026-10-20", buf, sizeof(buf)));
    CHECK_STR(buf, "Tue");
    CHECK(vigie_format_day_short("D+3", buf, sizeof(buf)));
    CHECK_STR(buf, "D+3");
    CHECK(vigie_format_lat_lon(48.8566, -2.35, buf, sizeof(buf)));
    CHECK_STR(buf, "48.86\xC2\xB0 N \xC2\xB7 2.35\xC2\xB0 W");

    VigieHazard h{};
    h.type = VigieHazardType::EARTHQUAKE;
    h.value = 6.3f;
    CHECK(vigie_format_hazard_value(h, buf, sizeof(buf)));
    CHECK_STR(buf, "M 6.3");
    h.type = VigieHazardType::SOLAR_STORM;
    h.value = 7.33f;
    CHECK(vigie_format_hazard_value(h, buf, sizeof(buf)));
    CHECK_STR(buf, "Kp 7");
    h.type = VigieHazardType::FLOOD;
    CHECK(!vigie_format_hazard_value(h, buf, sizeof(buf)));

    CHECK(vigie_is_night(kUtc, -14400));
    CHECK(!vigie_is_night(kUtc, 7200));
}

void test_french() {
    vigie_set_language(vigie_language_from_code("fr"));
    CHECK(vigie_language() == VigieLanguage::FR);
    char buf[64];
    CHECK_STR(vigie_weather_label(96), "Orage et grêle");
    CHECK_STR(vigie_hazard_name(VigieHazardType::EARTHQUAKE), "Séisme");
    CHECK_STR(vigie_tr(VigieText::NO_ACTIVE_ALERT), "Aucune alerte en cours");
    CHECK(vigie_format_decimal(6.26f, 1, buf, sizeof(buf)));
    CHECK_STR(buf, "6,3");
    CHECK(vigie_format_distance(999.6f, buf, sizeof(buf)));
    CHECK_STR(buf, "1 000 km");
    CHECK(vigie_format_age(100000, 100000 - 3 * 3600 - 10, buf, sizeof(buf)));
    CHECK_STR(buf, "il y a 3 h");
    CHECK(vigie_format_date(kUtc, 7200, buf, sizeof(buf)));
    CHECK_STR(buf, "mardi 20 octobre");
    CHECK(vigie_format_date(1790856000u, 0, buf, sizeof(buf)));
    CHECK_STR(buf, "jeudi 1er octobre");
    CHECK(vigie_format_date(kUtc, 32400 + 18 * 3600, buf, sizeof(buf)));  // past midnight through the offset
    CHECK_STR(buf, "mercredi 21 octobre");
    CHECK(vigie_format_day_short("2026-10-21", buf, sizeof(buf)));
    CHECK_STR(buf, "mer.");
    CHECK(vigie_format_lat_lon(-33.87, -151.21, buf, sizeof(buf)));
    CHECK_STR(buf, "33,87\xC2\xB0 S \xC2\xB7 151,21\xC2\xB0 O");
    vigie_set_language(vigie_language_from_code("en"));
    CHECK(vigie_language() == VigieLanguage::EN);
}

// Weather texts: a known value is formatted, an unknown one gives false (hidden on screen).
void test_weather_texts() {
    vigie_set_language(VigieLanguage::EN);
    char buf[96];
    CHECK(vigie_format_temperature(12.6f, buf, sizeof(buf)));
    CHECK_STR(buf, "13\xC2\xB0");
    CHECK(vigie_format_temperature(-0.4f, buf, sizeof(buf)));
    CHECK_STR(buf, "0\xC2\xB0");
    CHECK(!vigie_format_temperature(kVigieUnknown, buf, sizeof(buf)) && buf[0] == '\0');

    CHECK(vigie_format_wind(9.7f, 153, buf, sizeof(buf)));
    CHECK_STR(buf, "10 km/h SSE");
    CHECK(vigie_format_wind(9.7f, kVigieUnknownCode, buf, sizeof(buf)));
    CHECK_STR(buf, "10 km/h");
    CHECK(!vigie_format_wind(kVigieUnknown, 153, buf, sizeof(buf)));
    CHECK(!vigie_format_speed(kVigieUnknown, buf, sizeof(buf)));
    CHECK(vigie_format_percent(91, buf, sizeof(buf)));
    CHECK_STR(buf, "91%");
    CHECK(!vigie_format_percent(kVigieUnknownPct, buf, sizeof(buf)));

    VigieZone z{};
    CHECK(!vigie_format_condition(z, buf, sizeof(buf)));  // nothing known
    z.weather_code = 3;
    CHECK(vigie_format_condition(z, buf, sizeof(buf)));
    CHECK_STR(buf, "Overcast");
    z.feels_like_c = 11.6f;
    CHECK(vigie_format_condition(z, buf, sizeof(buf)));
    CHECK_STR(buf, "Overcast \xC2\xB7 feels like 12\xC2\xB0");
    z.weather_code = kVigieUnknownCode;
    CHECK(vigie_format_condition(z, buf, sizeof(buf)));
    CHECK_STR(buf, "feels like 12\xC2\xB0");

    VigieRainNowcast rain{};
    CHECK(!vigie_format_rain_next_hour(rain, buf, sizeof(buf)));  // unknown, not "none"
    rain.known = true;
    CHECK(vigie_format_rain_next_hour(rain, buf, sizeof(buf)));
    CHECK_STR(buf, "none");
    rain.expected = true;
    rain.starts_in_min = 15;
    CHECK(vigie_format_rain_next_hour(rain, buf, sizeof(buf)));
    CHECK_STR(buf, "in 15 min");
    rain.starts_in_min = 0;
    CHECK(vigie_format_rain_next_hour(rain, buf, sizeof(buf)));
    CHECK_STR(buf, "now");

    CHECK(vigie_format_air_quality(47, buf, sizeof(buf)));
    CHECK_STR(buf, "47 \xC2\xB7 Moderate");
    CHECK(!vigie_format_air_quality(kVigieUnknownCode, buf, sizeof(buf)));

    VigieMarine sea{};
    CHECK(!vigie_format_waves(sea, buf, sizeof(buf)));
    sea.valid = true;
    sea.wave_height_m = 3.4f;
    CHECK(vigie_format_waves(sea, buf, sizeof(buf)));
    CHECK_STR(buf, "3.4 m");
    sea.wave_period_s = 11.5f;
    CHECK(vigie_format_waves(sea, buf, sizeof(buf)));
    CHECK_STR(buf, "3.4 m \xC2\xB7 12 s");

    VigieDayForecast day{};
    CHECK(!vigie_format_day_rain(day, buf, sizeof(buf)));
    day.rain_mm = 0.0f;
    CHECK(!vigie_format_day_rain(day, buf, sizeof(buf)));  // a dry day shows nothing
    day.rain_mm = 5.6f;
    CHECK(vigie_format_day_rain(day, buf, sizeof(buf)));
    CHECK_STR(buf, "5.6 mm");
    day.rain_probability_pct = 98;
    CHECK(vigie_format_day_rain(day, buf, sizeof(buf)));
    CHECK_STR(buf, "98%");

    vigie_set_language(VigieLanguage::FR);
    CHECK(vigie_format_percent(98, buf, sizeof(buf)));
    CHECK_STR(buf, "98 %");
    CHECK(vigie_format_wind(30.2f, 270, buf, sizeof(buf)));
    CHECK_STR(buf, "30 km/h O");
    CHECK(vigie_format_waves(sea, buf, sizeof(buf)));
    CHECK_STR(buf, "3,4 m \xC2\xB7 12 s");
    rain.starts_in_min = 15;
    CHECK(vigie_format_rain_next_hour(rain, buf, sizeof(buf)));
    CHECK_STR(buf, "dans 15 min");
    z.weather_code = 63;
    CHECK(vigie_format_condition(z, buf, sizeof(buf)));
    CHECK_STR(buf, "Pluie \xC2\xB7 ressenti 12\xC2\xB0");
    CHECK_STR(vigie_tr(VigieText::ALERTS_UNKNOWN), "Sources d'alertes injoignables");
    vigie_set_language(VigieLanguage::EN);
}

}  // namespace

int main() {
    test_map();
    test_english();
    test_french();
    test_weather_texts();
    return test_report("vigie_text");
}
