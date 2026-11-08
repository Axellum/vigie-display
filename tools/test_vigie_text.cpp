// PC tests of firmware/core/vigie_text.cpp: map projection, labels, numbers, times, in
// English and French. Built and run by tests/test_firmware.py and the CI.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "test_check.h"
#include "test_vocabulary.h"
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

// ─── Sea texts (activities of a surf spot) ──────────────────────────────────

void test_sea_names_and_vocabulary() {
    vigie_set_language(VigieLanguage::EN);
    CHECK_STR(vigie_activity_name(VigieActivity::SURF), "Surf");
    CHECK_STR(vigie_activity_name(VigieActivity::KITE), "Kite / wing");
    CHECK_STR(vigie_activity_name(VigieActivity::SHORE_FISHING), "Shore fishing");
    CHECK_STR(vigie_activity_name(VigieActivity::COUNT_), "");
    CHECK_STR(vigie_verdict_label(VigieVerdict::GOOD), "Good");
    CHECK_STR(vigie_verdict_label(VigieVerdict::POOR), "Not advised");
    CHECK_STR(vigie_verdict_label(VigieVerdict::UNKNOWN), "");
    CHECK_STR(vigie_criterion_label(VigieCriterionId::SWELL_HEIGHT), "Swell");
    CHECK_STR(vigie_criterion_label(VigieCriterionId::COEFFICIENT), "Coefficient (est.)");
    CHECK_STR(vigie_criterion_label(VigieCriterionId::UNKNOWN), "");
    CHECK_STR(vigie_criterion_state_text(VigieCriterionId::SWELL_HEIGHT, VigieCriterionState::TOO_SMALL), "too small");
    CHECK_STR(vigie_criterion_state_text(VigieCriterionId::WIND_DIRECTION, VigieCriterionState::ONSHORE), "onshore");
    // A pair that is not in the contract has no text: nothing is shown, nothing is guessed.
    CHECK_STR(vigie_criterion_state_text(VigieCriterionId::TIDE, VigieCriterionState::TOO_SMALL), "");
    CHECK_STR(vigie_criterion_state_text(VigieCriterionId::UNKNOWN, VigieCriterionState::UNKNOWN), "");
    CHECK_STR(vigie_tr(VigieText::SEA_RULES_NOTICE), "Vigie's own rules, not for safety");

    vigie_set_language(VigieLanguage::FR);
    CHECK_STR(vigie_activity_name(VigieActivity::BOAT), "Bateau");
    CHECK_STR(vigie_activity_name(VigieActivity::SHORE_FISHING), "P\xC3\xAA" "che \xC3\xA0 pied");
    CHECK_STR(vigie_verdict_label(VigieVerdict::GOOD), "Bon");
    CHECK_STR(vigie_verdict_label(VigieVerdict::POOR), "D\xC3\xA9" "conseill\xC3\xA9");
    CHECK_STR(vigie_criterion_state_text(VigieCriterionId::SWELL_HEIGHT, VigieCriterionState::TOO_SMALL), "trop petite");
    CHECK_STR(vigie_criterion_state_text(VigieCriterionId::WIND, VigieCriterionState::OFFSHORE), "de terre");

    // Every pair of the contract's table has a text in both languages, and every criterion a label.
    for (const VocabularyRow& row : kContractVocabulary) {
        const VigieCriterionId criterion = vigie_criterion_from_name(row.criterion);
        for (int i = 0; i < 6 && row.states[i] != nullptr; ++i) {
            const VigieCriterionState state = vigie_criterion_state_from_name(row.states[i]);
            vigie_set_language(VigieLanguage::EN);
            const bool en = vigie_criterion_state_text(criterion, state)[0] != '\0';
            vigie_set_language(VigieLanguage::FR);
            const bool fr_text = vigie_criterion_state_text(criterion, state)[0] != '\0';
            if (!en || !fr_text) {
                std::printf("FAIL no text for %s.%s\n", row.criterion, row.states[i]);
                ++test_failures();
            }
        }
        CHECK(vigie_criterion_label(criterion)[0] != '\0');
    }
    vigie_set_language(VigieLanguage::EN);
}

void test_sea_values_and_lines() {
    char buf[64];
    VigieCriterion c{};
    c.value = 2.6f;
    c.unit = VigieUnit::METRES;

    vigie_set_language(VigieLanguage::EN);
    CHECK(vigie_format_criterion_value(c, buf, sizeof(buf)));
    CHECK_STR(buf, "2.6 m");
    c.value = 10.0f;
    c.unit = VigieUnit::SECONDS;
    CHECK(vigie_format_criterion_value(c, buf, sizeof(buf)) && std::strcmp(buf, "10 s") == 0);
    c.value = 21.0f;
    c.unit = VigieUnit::DEGREES;
    CHECK(vigie_format_criterion_value(c, buf, sizeof(buf)) && std::strcmp(buf, "21\xC2\xB0") == 0);
    c.value = 18.7f;
    c.unit = VigieUnit::KMH;
    CHECK(vigie_format_criterion_value(c, buf, sizeof(buf)) && std::strcmp(buf, "19 km/h") == 0);
    c.value = 11.9f;
    c.unit = VigieUnit::KNOTS;
    CHECK(vigie_format_criterion_value(c, buf, sizeof(buf)) && std::strcmp(buf, "11.9 kn") == 0);
    c.value = 3.0f;
    c.unit = VigieUnit::BEAUFORT;
    CHECK(vigie_format_criterion_value(c, buf, sizeof(buf)) && std::strcmp(buf, "Force 3") == 0);
    c.value = 102.0f;
    c.unit = VigieUnit::MINUTES;
    CHECK(vigie_format_criterion_value(c, buf, sizeof(buf)) && std::strcmp(buf, "102 min") == 0);
    c.value = 91.0f;
    c.unit = VigieUnit::NONE;
    CHECK(vigie_format_criterion_value(c, buf, sizeof(buf)) && std::strcmp(buf, "91") == 0);
    // Unknown value or unit: nothing is shown.
    c.value = kVigieUnknown;
    CHECK(!vigie_format_criterion_value(c, buf, sizeof(buf)) && buf[0] == '\0');
    c.value = 5.0f;
    c.unit = VigieUnit::UNKNOWN;
    CHECK(!vigie_format_criterion_value(c, buf, sizeof(buf)) && buf[0] == '\0');
    c.unit = VigieUnit::METRES;
    c.value = 2.6f;
    char tiny[3];
    CHECK(!vigie_format_criterion_value(c, tiny, sizeof(tiny)));  // too small a buffer
    vigie_set_language(VigieLanguage::FR);
    CHECK(vigie_format_criterion_value(c, buf, sizeof(buf)) && std::strcmp(buf, "2,6 m") == 0);
    c.unit = VigieUnit::KNOTS;
    c.value = 11.9f;
    CHECK(vigie_format_criterion_value(c, buf, sizeof(buf)) && std::strcmp(buf, "11,9 kn") == 0);

    // Reason.
    VigieActivityForecast a{};
    a.reason_criterion = VigieCriterionId::SWELL_HEIGHT;
    a.reason_state = VigieCriterionState::TOO_SMALL;
    vigie_set_language(VigieLanguage::EN);
    CHECK(vigie_format_reason(a, buf, sizeof(buf)) && std::strcmp(buf, "Swell: too small") == 0);
    vigie_set_language(VigieLanguage::FR);
    CHECK(vigie_format_reason(a, buf, sizeof(buf)) && std::strcmp(buf, "Houle : trop petite") == 0);
    a.reason_criterion = VigieCriterionId::VIGILANCE;
    a.reason_state = VigieCriterionState::RED;
    vigie_set_language(VigieLanguage::EN);
    CHECK(vigie_format_reason(a, buf, sizeof(buf)) && std::strcmp(buf, "Coastal warning: red") == 0);
    a.reason_criterion = VigieCriterionId::UNKNOWN;
    CHECK(!vigie_format_reason(a, buf, sizeof(buf)) && buf[0] == '\0');
    a.reason_criterion = VigieCriterionId::TIDE;  // a pair the contract does not list
    a.reason_state = VigieCriterionState::TOO_SMALL;
    CHECK(!vigie_format_reason(a, buf, sizeof(buf)));

    // Slot, local time.
    CHECK(vigie_format_slot(1794132000u, 1794139200u, 3600, buf, sizeof(buf)) && std::strcmp(buf, "11:00\xE2\x80\x93" "13:00") == 0);
    CHECK(vigie_format_slot(1794132000u, 1794139200u, 0, buf, sizeof(buf)) && std::strcmp(buf, "10:00\xE2\x80\x93" "12:00") == 0);
    CHECK(!vigie_format_slot(0, 1794139200u, 0, buf, sizeof(buf)) && buf[0] == '\0');
    CHECK(!vigie_format_slot(1794139200u, 1794132000u, 0, buf, sizeof(buf)));

    // Tide.
    VigieTideExtreme high{};
    high.high = true;
    high.utc = 1794149460u;
    high.height_m = 1.71f;
    CHECK(vigie_format_tide_extreme(high, 3600, buf, sizeof(buf)) && std::strcmp(buf, "High water 15:51 \xC2\xB7 1.7 m") == 0);
    VigieTideExtreme low = high;
    low.high = false;
    low.height_m = kVigieUnknown;
    CHECK(vigie_format_tide_extreme(low, 3600, buf, sizeof(buf)) && std::strcmp(buf, "Low water 15:51") == 0);
    vigie_set_language(VigieLanguage::FR);
    CHECK(vigie_format_tide_extreme(high, 3600, buf, sizeof(buf)) && std::strcmp(buf, "Pleine mer 15:51 \xC2\xB7 1,7 m") == 0);
    high.utc = 0;
    CHECK(!vigie_format_tide_extreme(high, 0, buf, sizeof(buf)) && buf[0] == '\0');
    VigieTideCoefficient k{};
    k.value = 84;
    CHECK(vigie_format_tide_coefficient(k, buf, sizeof(buf)) && std::strcmp(buf, "Coefficient 84 (estim\xC3\xA9)") == 0);
    vigie_set_language(VigieLanguage::EN);
    CHECK(vigie_format_tide_coefficient(k, buf, sizeof(buf)) && std::strcmp(buf, "Coefficient 84 (estimated)") == 0);
    k.value = 0;
    CHECK(!vigie_format_tide_coefficient(k, buf, sizeof(buf)) && buf[0] == '\0');
}

void test_sea_warning_and_status() {
    char buf[96];
    VigieSea sea{};
    vigie_set_language(VigieLanguage::EN);
    CHECK(!vigie_format_warning(sea, 0, buf, sizeof(buf)));  // no sea data at all
    sea.valid = true;
    // The warnings could not be read: the screen says so, it does not say "nothing to fear".
    sea.warnings_known = false;
    CHECK(vigie_format_warning(sea, 0, buf, sizeof(buf)) && std::strcmp(buf, "Coastal warnings unavailable") == 0);
    sea.warnings_known = true;
    CHECK(!vigie_format_warning(sea, 0, buf, sizeof(buf)) && buf[0] == '\0');  // none stands
    sea.warning_present = true;
    sea.warning_level = VigieSeverity::GREEN;
    CHECK(!vigie_format_warning(sea, 0, buf, sizeof(buf)));
    sea.warning_level = VigieSeverity::ORANGE;
    sea.warning_until_utc = 1794139200u;
    CHECK(vigie_format_warning(sea, 3600, buf, sizeof(buf)) && std::strcmp(buf, "Orange coastal warning \xC2\xB7 until 13:00") == 0);
    sea.warning_level = VigieSeverity::RED;
    sea.warning_until_utc = 0;
    CHECK(vigie_format_warning(sea, 0, buf, sizeof(buf)) && std::strcmp(buf, "Red coastal warning") == 0);
    vigie_set_language(VigieLanguage::FR);
    CHECK(vigie_format_warning(sea, 0, buf, sizeof(buf)) && std::strcmp(buf, "Vigilance c\xC3\xB4" "ti\xC3\xA8" "re rouge") == 0);
    sea.warning_level = VigieSeverity::YELLOW;
    sea.warning_until_utc = 1794139200u;
    CHECK(vigie_format_warning(sea, 0, buf, sizeof(buf)) &&
          std::strcmp(buf, "Vigilance c\xC3\xB4" "ti\xC3\xA8" "re jaune \xC2\xB7 jusqu'\xC3\xA0 12:00") == 0);

    // Age of the data.
    vigie_set_language(VigieLanguage::EN);
    sea.generated_utc = 1794133609u;
    CHECK(vigie_format_sea_status(sea, VigieSeaFreshness::NONE, 1794133609u, buf, sizeof(buf)) &&
          std::strcmp(buf, "Sea data unavailable") == 0);
    CHECK(vigie_format_sea_status(sea, VigieSeaFreshness::EXPIRED, 1794133609u + 99999u, buf, sizeof(buf)) &&
          std::strcmp(buf, "Sea data too old") == 0);
    CHECK(vigie_format_sea_status(sea, VigieSeaFreshness::STALE, 1794133609u + 720u, buf, sizeof(buf)) &&
          std::strcmp(buf, "Sea data \xC2\xB7 12 min ago") == 0);
    vigie_set_language(VigieLanguage::FR);
    CHECK(vigie_format_sea_status(sea, VigieSeaFreshness::STALE, 1794133609u + 720u, buf, sizeof(buf)) &&
          std::strcmp(buf, "Mer \xC2\xB7 il y a 12 min") == 0);
    sea.stale = true;  // the API served the last answer of a provider: say so, whatever the age
    vigie_set_language(VigieLanguage::EN);
    CHECK(vigie_format_sea_status(sea, VigieSeaFreshness::STALE, 1794133609u + 5u, buf, sizeof(buf)) &&
          std::strcmp(buf, "Some sea data is from an earlier reading") == 0);
    CHECK(!vigie_format_sea_status(sea, VigieSeaFreshness::FRESH, 1794133609u, buf, sizeof(buf)) && buf[0] == '\0');
    vigie_set_language(VigieLanguage::EN);
}

// The sentences the sea page adds, and the banner that says the most important thing.
void test_sea_page_texts() {
    char buf[96];
    vigie_set_language(VigieLanguage::EN);
    CHECK(vigie_format_score(7, 8, buf, sizeof(buf)) && std::strcmp(buf, "Score 7 of 8") == 0);
    CHECK(vigie_format_score(0, 8, buf, sizeof(buf)) && std::strcmp(buf, "Score 0 of 8") == 0);  // a real zero
    CHECK(!vigie_format_score(-1, 8, buf, sizeof(buf)) && buf[0] == '\0');  // unknown is not 0
    CHECK(!vigie_format_score(5, -1, buf, sizeof(buf)) && !vigie_format_score(5, 0, buf, sizeof(buf)));
    CHECK(!vigie_format_score(9, 8, buf, sizeof(buf)));
    VigieCriterion c{};
    CHECK(!vigie_format_criterion_points(c, buf, sizeof(buf)) && buf[0] == '\0');
    c.points = 2;
    c.max_points = 2;
    CHECK(vigie_format_criterion_points(c, buf, sizeof(buf)) && std::strcmp(buf, "2/2") == 0);
    c.points = 0;
    CHECK(vigie_format_criterion_points(c, buf, sizeof(buf)) && std::strcmp(buf, "0/2") == 0);
    c.max_points = -1;
    CHECK(!vigie_format_criterion_points(c, buf, sizeof(buf)));
    CHECK(vigie_format_detail_title(VigieActivity::SURF, buf, sizeof(buf)) && std::strcmp(buf, "Surf \xC2\xB7 detail") == 0);
    CHECK(!vigie_format_detail_title(VigieActivity::COUNT_, buf, sizeof(buf)) && buf[0] == '\0');
    CHECK(vigie_format_detail_hour(1794132000u, 3600, buf, sizeof(buf)) && std::strcmp(buf, "for 11:00") == 0);
    CHECK(!vigie_format_detail_hour(0, 0, buf, sizeof(buf)));
    vigie_set_language(VigieLanguage::FR);
    CHECK(vigie_format_score(7, 8, buf, sizeof(buf)) && std::strcmp(buf, "Note 7 sur 8") == 0);
    CHECK(vigie_format_detail_title(VigieActivity::SURF, buf, sizeof(buf)) && std::strcmp(buf, "Surf \xC2\xB7 d\xC3\xA9" "tail") == 0);
    CHECK(vigie_format_detail_hour(1794132000u, 3600, buf, sizeof(buf)) && std::strcmp(buf, "pour 11:00") == 0);

    // Every new text has both languages.
    for (int language = 0; language < 2; ++language) {
        vigie_set_language(language == 0 ? VigieLanguage::EN : VigieLanguage::FR);
        for (int id = static_cast<int>(VigieText::SEA_PAGE); id < static_cast<int>(VigieText::COUNT_); ++id) {
            CHECK(vigie_tr(static_cast<VigieText>(id))[0] != '\0');
        }
    }
    vigie_set_language(VigieLanguage::EN);
    CHECK_STR(vigie_tr(VigieText::SEA_PAGE), "Sea");
    CHECK_STR(vigie_tr(VigieText::WORLD_PAGE), "World");
    vigie_set_language(VigieLanguage::FR);
    CHECK_STR(vigie_tr(VigieText::SEA_PAGE), "Mer");
    CHECK_STR(vigie_tr(VigieText::WORLD_PAGE), "Monde");

    // The banner. Nothing received: it says so.
    vigie_set_language(VigieLanguage::EN);
    VigieSea sea{};
    CHECK(vigie_format_sea_banner(sea, VigieSeaFreshness::NONE, 0, 0, buf, sizeof(buf)) == VigieSeaBanner::STATUS &&
          std::strcmp(buf, "Sea data unavailable") == 0);
    sea.valid = true;
    sea.generated_utc = 1794133609u;
    sea.warnings_known = true;
    // A fresh sea with no warning: nothing to say.
    CHECK(vigie_format_sea_banner(sea, VigieSeaFreshness::FRESH, 1794133609u, 0, buf, sizeof(buf)) == VigieSeaBanner::NONE &&
          buf[0] == '\0');
    // A warning is the banner, with its level.
    sea.warning_present = true;
    sea.warning_level = VigieSeverity::ORANGE;
    sea.warning_until_utc = 1794139200u;
    CHECK(vigie_format_sea_banner(sea, VigieSeaFreshness::FRESH, 1794133609u, 3600, buf, sizeof(buf)) == VigieSeaBanner::WARNING &&
          std::strcmp(buf, "Orange coastal warning \xC2\xB7 until 13:00") == 0);
    // A warning wins over the age of a sea that is some minutes old.
    CHECK(vigie_format_sea_banner(sea, VigieSeaFreshness::STALE, 1794133609u + 720u, 3600, buf, sizeof(buf)) == VigieSeaBanner::WARNING);
    // A sea too old shows no verdict, so a warning of that data is not current: the age is the banner.
    CHECK(vigie_format_sea_banner(sea, VigieSeaFreshness::EXPIRED, 1794133609u + 99999u, 3600, buf, sizeof(buf)) == VigieSeaBanner::STATUS &&
          std::strcmp(buf, "Sea data too old") == 0);
    // Warnings that could not be read are said, never an all-clear.
    sea.warnings_known = false;
    sea.warning_present = false;
    CHECK(vigie_format_sea_banner(sea, VigieSeaFreshness::FRESH, 1794133609u, 0, buf, sizeof(buf)) == VigieSeaBanner::WARNING_UNKNOWN &&
          std::strcmp(buf, "Coastal warnings unavailable") == 0);
    // No warning, but the data is some minutes old: the age.
    sea.warnings_known = true;
    CHECK(vigie_format_sea_banner(sea, VigieSeaFreshness::STALE, 1794133609u + 720u, 0, buf, sizeof(buf)) == VigieSeaBanner::STATUS &&
          std::strcmp(buf, "Sea data \xC2\xB7 12 min ago") == 0);
    vigie_set_language(VigieLanguage::EN);
}

}  // namespace

int main() {
    test_map();
    test_english();
    test_french();
    test_weather_texts();
    test_sea_names_and_vocabulary();
    test_sea_values_and_lines();
    test_sea_warning_and_status();
    test_sea_page_texts();
    return test_report("vigie_text");
}
