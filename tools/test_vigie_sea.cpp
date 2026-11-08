// PC tests of firmware/core/vigie_sea.cpp: the SEA / ACT / CRIT lines of vigie-api's embedded frame,
// on a real frame (tests/fixtures/api_frame_activities.txt, path given as argv[2], argv[1] being the
// frame without the block) and on small
// signed frames for the edge cases. Built and run by tests/test_firmware.py and the CI.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "test_check.h"
#include "test_vocabulary.h"
#include "vigie_sea.h"

namespace {

char g_real[32768];

size_t read_file(const char* path, char* out, size_t out_size) {
    std::FILE* f = std::fopen(path, "rb");
    if (f == nullptr) return 0;
    const size_t n = std::fread(out, 1, out_size - 1, f);
    std::fclose(f);
    out[n] = '\0';
    return n;
}

bool close_to(float a, float b) { return std::fabs(a - b) < 0.001f; }

// One ACT line with its 12 fields (the arguments are the fields 2 to 11, as text so that "" is empty).
struct Line {
    char text[512];
};
Line act(const char* activity, const char* available, const char* verdict, const char* reason, const char* start,
         const char* end, const char* hours, const char* daylight, const char* detail, const char* points,
         const char* out_of) {
    Line line{};
    std::snprintf(line.text, sizeof(line.text), "ACT|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s\n", activity, available, verdict,
                  reason, start, end, hours, daylight, detail, points, out_of);
    return line;
}

// "<body>END|<CRC32>\n" in `out`.
void sign(const char* body, char* out, size_t out_size) {
    std::snprintf(out, out_size, "%sEND|%08X\n", body, static_cast<unsigned>(vigie_crc32(body, std::strlen(body))));
}

// A minimal good SEA line (18 fields): the first hour 1794132000, generated 1794133609.
const char* kSea = "SEA|Spot|Town|1794133609|1794132000|0|12.0|25.0|1794120636|1794156478|||||ok|||\n";

constexpr const char* kAllDay = "111111100000000000000111";

// ─── The real frame ─────────────────────────────────────────────────────────

void test_real_frame() {
    VigieSea sea{};
    CHECK(vigie_update_sea(g_real, sea) == VigieSeaUpdate::UPDATED);
    CHECK(sea.valid);
    CHECK_STR(sea.name, "Plage Sud (Point d'Or)");
    CHECK_STR(sea.place, "Soorts-Hossegor");
    CHECK(sea.generated_utc == 1794133609u && sea.first_hour_utc == 1794132000u && !sea.stale);
    CHECK(close_to(sea.wind_min_kn, 12.0f) && close_to(sea.wind_max_kn, 25.0f));
    CHECK(sea.sunrise_utc == 1794120636u && sea.sunset_utc == 1794156478u);
    CHECK(sea.sunrise2_utc == 1794207116u && sea.sunset2_utc == 1794242808u);

    CHECK(sea.tide_count == 6);
    CHECK(!sea.tide[0].high && sea.tide[0].utc == 1794126780u && close_to(sea.tide[0].height_m, -1.66f));
    CHECK(sea.tide[1].high && sea.tide[1].utc == 1794149460u && close_to(sea.tide[1].height_m, 1.71f));
    CHECK(sea.coefficient_count == 3);
    CHECK(sea.coefficients[0].high_water_utc == 1794150900u && sea.coefficients[0].value == 83);
    CHECK(sea.coefficients[2].value == 83 && sea.coefficients[1].value == 84);
    // Warnings read and none stands: known, nothing to fear.
    CHECK(sea.warnings_known && !sea.warning_present);

    const VigieActivityForecast& surf = sea.activities[static_cast<int>(VigieActivity::SURF)];
    CHECK(surf.present && surf.available && surf.verdict == VigieVerdict::POOR);
    CHECK(surf.reason_criterion == VigieCriterionId::SWELL_HEIGHT && surf.reason_state == VigieCriterionState::TOO_SMALL);
    CHECK(surf.slot_start_utc == 0 && surf.slot_end_utc == 0);  // poor: no slot to go
    for (int h = 0; h < kVigieSeaHours; ++h) {
        CHECK(surf.hours[h] == VigieVerdict::POOR);
        CHECK(surf.daylight[h] == (kAllDay[h] == '1' ? VigieDaylight::DAY : VigieDaylight::NIGHT));
    }
    CHECK(surf.detail_utc == 1794132000u && surf.points == 4 && surf.out_of == 8);
    CHECK(surf.criterion_count == 5);
    CHECK(surf.criteria[0].id == VigieCriterionId::SWELL_HEIGHT && close_to(surf.criteria[0].value, 0.4f));
    CHECK(surf.criteria[0].unit == VigieUnit::METRES && surf.criteria[0].state == VigieCriterionState::TOO_SMALL);
    CHECK(surf.criteria[0].verdict == VigieVerdict::POOR && surf.criteria[0].points == 0 && surf.criteria[0].max_points == 2);
    CHECK(surf.criteria[3].id == VigieCriterionId::WIND && surf.criteria[3].unit == VigieUnit::KMH);
    CHECK(surf.criteria[3].state == VigieCriterionState::OFFSHORE && surf.criteria[3].points == 2);
    // The tide has a value and no rule: unknown state, verdict and points; it counts for nothing.
    const VigieCriterion& tide = surf.criteria[4];
    CHECK(tide.id == VigieCriterionId::TIDE && close_to(tide.value, -1.2f) && tide.unit == VigieUnit::METRES);
    CHECK(tide.state == VigieCriterionState::UNKNOWN && tide.verdict == VigieVerdict::UNKNOWN);
    CHECK(tide.points == -1 && tide.max_points == 2);

    const VigieActivityForecast& kite = sea.activities[static_cast<int>(VigieActivity::KITE)];
    CHECK(kite.verdict == VigieVerdict::POSSIBLE && kite.reason_criterion == VigieCriterionId::WIND);
    CHECK(kite.reason_state == VigieCriterionState::A_BIT_LIGHT);
    CHECK(kite.slot_start_utc == 1794132000u && kite.slot_end_utc == 1794139200u);
    CHECK(kite.hours[0] == VigieVerdict::POSSIBLE && kite.hours[2] == VigieVerdict::POOR);
    CHECK(kite.points == -1 && kite.out_of == -1);  // surf only
    CHECK(kite.criterion_count == 3 && kite.criteria[0].unit == VigieUnit::KNOTS && close_to(kite.criteria[0].value, 9.7f));

    const VigieActivityForecast& boat = sea.activities[static_cast<int>(VigieActivity::BOAT)];
    CHECK(boat.verdict == VigieVerdict::GOOD && boat.reason_criterion == VigieCriterionId::UNKNOWN);  // good: no reason
    CHECK(boat.slot_start_utc == 1794132000u && boat.slot_end_utc == 1794157200u && boat.criterion_count == 3);
    CHECK(boat.criteria[0].unit == VigieUnit::BEAUFORT && boat.criteria[0].state == VigieCriterionState::UP_TO_MODERATE);

    const VigieActivityForecast& fishing = sea.activities[static_cast<int>(VigieActivity::SHORE_FISHING)];
    CHECK(fishing.verdict == VigieVerdict::GOOD && fishing.slot_start_utc == 1794207600u);
    CHECK(fishing.hours[9] == VigieVerdict::GOOD && fishing.hours[10] == VigieVerdict::GOOD && fishing.hours[0] == VigieVerdict::POOR);
    CHECK(fishing.criterion_count == 4 && fishing.criteria[1].id == VigieCriterionId::COEFFICIENT);
    CHECK(fishing.criteria[1].unit == VigieUnit::NONE && close_to(fishing.criteria[1].value, 84.0f));
    CHECK(fishing.criteria[1].state == VigieCriterionState::LARGE);

    // Reading: the hour of now, the best activity (boat and shore fishing are good: the first wins), the tide.
    CHECK(vigie_sea_hour_index(sea, 1794133609u) == 0);
    CHECK(vigie_sea_hour_index(sea, 1794132000u + 5 * 3600u + 59) == 5);
    CHECK(vigie_sea_hour_index(sea, 1794132000u + 24 * 3600u) == -1);
    CHECK(vigie_sea_hour_index(sea, 1794131999u) == -1);
    CHECK(vigie_sea_best_activity(sea) == static_cast<int>(VigieActivity::BOAT));
    CHECK(vigie_sea_next_tide(sea, 1794133609u) == 1);
    CHECK(vigie_sea_next_tide(sea, 1794238020u) == 5);
    CHECK(vigie_sea_next_tide(sea, 1794238021u) == -1);

    // The frame is refused when a byte moved, and the sea is kept.
    static char damaged[32768];
    std::strcpy(damaged, g_real);
    char* act = std::strstr(damaged, "ACT|boat");
    CHECK(act != nullptr);
    if (act != nullptr) act[4] = 'x';
    VigieSea kept = sea;
    CHECK(vigie_update_sea(damaged, kept) == VigieSeaUpdate::REFUSED);
    CHECK(!vigie_decode_sea(damaged, kept) && kept.generated_utc == sea.generated_utc);
}

// ─── Freshness ──────────────────────────────────────────────────────────────

void test_freshness() {
    VigieSea sea{};
    CHECK(vigie_sea_freshness(sea, 1794133609u) == VigieSeaFreshness::NONE);
    CHECK(vigie_decode_sea("HEAD|1|2|3\n", sea) == false);  // no SEA line
    char frame[512];
    sign(kSea, frame, sizeof(frame));
    CHECK(vigie_decode_sea(frame, sea));
    const uint32_t g = sea.generated_utc;
    CHECK(vigie_sea_freshness(sea, g) == VigieSeaFreshness::FRESH);
    CHECK(vigie_sea_freshness(sea, g - 5) == VigieSeaFreshness::FRESH);  // a clock a little behind the server
    CHECK(vigie_sea_freshness(sea, g + kVigieSeaStaleAfterS) == VigieSeaFreshness::FRESH);
    CHECK(vigie_sea_freshness(sea, g + kVigieSeaStaleAfterS + 1) == VigieSeaFreshness::STALE);
    CHECK(vigie_sea_freshness(sea, g + kVigieSeaExpiredAfterS) == VigieSeaFreshness::STALE);
    CHECK(vigie_sea_freshness(sea, g + kVigieSeaExpiredAfterS + 1) == VigieSeaFreshness::EXPIRED);
    CHECK(vigie_sea_freshness(sea, 0) == VigieSeaFreshness::STALE);  // no clock: the age is not known
    // The 24 hours are over, whatever the age of the frame.
    VigieSea over = sea;
    over.generated_utc = sea.first_hour_utc + 24 * 3600u - 10;
    CHECK(vigie_sea_freshness(over, sea.first_hour_utc + 24 * 3600u) == VigieSeaFreshness::EXPIRED);
    CHECK(vigie_sea_freshness(over, sea.first_hour_utc + 24 * 3600u - 5) == VigieSeaFreshness::FRESH);
    // The API says it serves the last answer of a provider.
    VigieSea served = sea;
    served.stale = true;
    CHECK(vigie_sea_freshness(served, g) == VigieSeaFreshness::STALE);
    // No hour for any activity (first_hour_utc 0): only the age counts.
    VigieSea no_hour = sea;
    no_hour.first_hour_utc = 0;
    CHECK(vigie_sea_freshness(no_hour, g + 10) == VigieSeaFreshness::FRESH);
    CHECK(vigie_sea_hour_index(no_hour, g) == -1);
}

// ─── The block is optional and the sea is kept ──────────────────────────────

void test_update_keeps_the_sea() {
    VigieSea sea{};
    char with_block[2048], without_block[512];
    std::snprintf(with_block, sizeof(with_block), "HEAD|1794133600|1.0|0|1.1|tab5|||||||\n%s", kSea);
    char body[2048];
    std::snprintf(body, sizeof(body), "%s", with_block);
    sign(body, with_block, sizeof(with_block));
    sign("HEAD|1794133700|1.0|0|1.1|tab5|||||||\n", without_block, sizeof(without_block));

    CHECK(vigie_update_sea(with_block, sea) == VigieSeaUpdate::UPDATED);
    CHECK(sea.generated_utc == 1794133609u);
    // A good frame without the block (no spot asked, or the swell could not be read): unknown, the
    // previous sea stays and gets older.
    CHECK(vigie_update_sea(without_block, sea) == VigieSeaUpdate::NO_BLOCK);
    CHECK(sea.valid && sea.generated_utc == 1794133609u);
    // Not a valid frame.
    CHECK(vigie_update_sea("", sea) == VigieSeaUpdate::REFUSED);
    CHECK(vigie_update_sea(nullptr, sea) == VigieSeaUpdate::REFUSED);
    char bad[512];
    std::snprintf(bad, sizeof(bad), "%sEND|DEADBEEF\n", kSea);
    CHECK(vigie_update_sea(bad, sea) == VigieSeaUpdate::REFUSED && sea.generated_utc == 1794133609u);
    // A SEA line that cannot be read (no name, no time): refused, the sea is kept.
    char unreadable[512];
    sign("SEA||Town|1794133609|1794132000|0|12.0|25.0|||||||ok|||\n", unreadable, sizeof(unreadable));
    CHECK(vigie_update_sea(unreadable, sea) == VigieSeaUpdate::REFUSED && sea.generated_utc == 1794133609u);
    sign("SEA|Spot|Town||1794132000|0|12.0|25.0|||||||ok|||\n", unreadable, sizeof(unreadable));
    CHECK(vigie_update_sea(unreadable, sea) == VigieSeaUpdate::REFUSED);
    // A SEA line shorter than the contract: refused (a column could be read for another).
    sign("SEA|Spot|Town|1794133609|1794132000|0|12.0|25.0\n", unreadable, sizeof(unreadable));
    CHECK(vigie_update_sea(unreadable, sea) == VigieSeaUpdate::REFUSED);
}

// ─── Empty is unknown ───────────────────────────────────────────────────────

void test_empty_fields_are_unknown() {
    VigieSea sea{};
    char frame[2048];
    // No tide model near, no sun, no hour: every optional field empty. Warnings could not be read.
    char body[2048];
    std::snprintf(body, sizeof(body), "SEA|Spot||1794133609||0|||||||||unknown|||\n%s",
                  act("surf", "1", "", "", "", "", "", "", "", "", "").text);
    sign(body, frame, sizeof(frame));
    CHECK(vigie_decode_sea(frame, sea));
    CHECK_STR(sea.place, "");
    CHECK(sea.first_hour_utc == 0 && !vigie_known(sea.wind_min_kn) && !vigie_known(sea.wind_max_kn));
    CHECK(sea.sunrise_utc == 0 && sea.sunset_utc == 0 && sea.sunrise2_utc == 0 && sea.sunset2_utc == 0);
    CHECK(sea.tide_count == 0 && sea.coefficient_count == 0);
    CHECK(!sea.warnings_known && !sea.warning_present);  // "could not tell", not "nothing to fear"
    const VigieActivityForecast& surf = sea.activities[0];
    CHECK(surf.present && surf.available && surf.verdict == VigieVerdict::UNKNOWN);
    CHECK(surf.slot_start_utc == 0 && surf.detail_utc == 0 && surf.points == -1 && surf.out_of == -1);
    for (int h = 0; h < kVigieSeaHours; ++h) CHECK(surf.hours[h] == VigieVerdict::UNKNOWN && surf.daylight[h] == VigieDaylight::UNKNOWN);
    CHECK(!sea.activities[1].present);  // not received: not "good", not "poor"
    CHECK(vigie_sea_best_activity(sea) == -1);

    // An activity that cannot be judged here: its other fields are not read.
    std::snprintf(body, sizeof(body), "%s%s", kSea,
                  act("shore_fishing", "0", "good", "wind_force.strong", "1794132000", "1794139200", "GGGGGGGGGGGGGGGGGGGGGGGG",
                      "111111111111111111111111", "1794132000", "4", "8").text);
    sign(body, frame, sizeof(frame));
    CHECK(vigie_decode_sea(frame, sea));
    const VigieActivityForecast& fishing = sea.activities[static_cast<int>(VigieActivity::SHORE_FISHING)];
    CHECK(fishing.present && !fishing.available && fishing.verdict == VigieVerdict::UNKNOWN);
    CHECK(fishing.slot_start_utc == 0 && fishing.hours[0] == VigieVerdict::UNKNOWN && fishing.points == -1);
    CHECK(vigie_sea_best_activity(sea) == -1);

    // Strips that are not 24 characters long say nothing (no hour is invented), a '-' is no verdict.
    std::snprintf(body, sizeof(body), "%s%s%s", kSea,
                  act("surf", "1", "good", "", "1794132000", "1794135600", "GGG", "111", "1794132000", "", "").text,
                  act("kite", "1", "good", "", "1794132000", "1794135600", "G-PX--------------------",
                      "1-0---------------------", "1794132000", "", "").text);
    sign(body, frame, sizeof(frame));
    CHECK(vigie_decode_sea(frame, sea));
    CHECK(sea.activities[0].hours[0] == VigieVerdict::UNKNOWN && sea.activities[0].daylight[0] == VigieDaylight::UNKNOWN);
    const VigieActivityForecast& kite = sea.activities[1];
    CHECK(kite.hours[0] == VigieVerdict::GOOD && kite.hours[1] == VigieVerdict::UNKNOWN);
    CHECK(kite.hours[2] == VigieVerdict::POSSIBLE && kite.hours[3] == VigieVerdict::POOR && kite.hours[4] == VigieVerdict::UNKNOWN);
    CHECK(kite.daylight[0] == VigieDaylight::DAY && kite.daylight[1] == VigieDaylight::UNKNOWN && kite.daylight[2] == VigieDaylight::NIGHT);
    // A slot that ends before it starts is no slot.
    std::snprintf(body, sizeof(body), "%s%s", kSea,
                  act("boat", "1", "good", "", "1794139200", "1794132000", "GGGGGGGGGGGGGGGGGGGGGGGG",
                      "111111111111111111111111", "1794132000", "", "").text);
    sign(body, frame, sizeof(frame));
    CHECK(vigie_decode_sea(frame, sea));
    CHECK(sea.activities[2].slot_start_utc == 0 && sea.activities[2].slot_end_utc == 0);
}

// ─── Coastal warnings ───────────────────────────────────────────────────────

void test_warnings() {
    VigieSea sea{};
    char frame[1024];
    sign("SEA|Spot|Town|1794133609|1794132000|0|12.0|25.0|||||||ok|ORANGE|1794139200|meteoalarm\n", frame, sizeof(frame));
    CHECK(vigie_decode_sea(frame, sea));
    CHECK(sea.warnings_known && sea.warning_present && sea.warning_level == VigieSeverity::ORANGE);
    CHECK(sea.warning_until_utc == 1794139200u);
    CHECK_STR(sea.warning_source, "meteoalarm");
    // No end given (one of the warnings has none): the end is unknown.
    sign("SEA|Spot|Town|1794133609|1794132000|0|12.0|25.0|||||||ok|RED||meteoalarm\n", frame, sizeof(frame));
    CHECK(vigie_decode_sea(frame, sea));
    CHECK(sea.warning_present && sea.warning_level == VigieSeverity::RED && sea.warning_until_utc == 0);
    // A level this firmware does not know: it cannot say "nothing to fear".
    sign("SEA|Spot|Town|1794133609|1794132000|0|12.0|25.0|||||||ok|PURPLE||x\n", frame, sizeof(frame));
    CHECK(vigie_decode_sea(frame, sea));
    CHECK(!sea.warnings_known && !sea.warning_present);
    // Status unknown with a level: the status wins.
    sign("SEA|Spot|Town|1794133609|1794132000|0|12.0|25.0|||||||unknown|RED||x\n", frame, sizeof(frame));
    CHECK(vigie_decode_sea(frame, sea));
    CHECK(!sea.warnings_known && !sea.warning_present);
}

// ─── Caps, order, unknown tags ──────────────────────────────────────────────

void test_caps_and_order() {
    VigieSea sea{};
    static char frame[4096];
    static char body[4096];
    // Ten extremes and six coefficients: the first 8 and 4 are kept.
    char tide[400] = "";
    char coefficients[200] = "";
    for (int i = 0; i < 10; ++i) {
        char item[48];
        std::snprintf(item, sizeof(item), "%s%c:%u:%d.50", i == 0 ? "" : ",", i % 2 == 0 ? 'L' : 'H', 1794130000u + 21000u * static_cast<unsigned>(i), i);
        std::strcat(tide, item);
    }
    for (int i = 0; i < 6; ++i) {
        char item[48];
        std::snprintf(item, sizeof(item), "%s%u:%d", i == 0 ? "" : ",", 1794140000u + 44000u * static_cast<unsigned>(i), 70 + i);
        std::strcat(coefficients, item);
    }
    std::snprintf(body, sizeof(body), "SEA|Spot|Town|1794133609|1794132000|0|12.0|25.0|||||%s|%s|ok|||\n", tide, coefficients);
    sign(body, frame, sizeof(frame));
    CHECK(vigie_decode_sea(frame, sea));
    CHECK(sea.tide_count == kVigieMaxTideExtremes && kVigieMaxTideExtremes == 8);
    CHECK(sea.coefficient_count == kVigieMaxTideCoefficients && kVigieMaxTideCoefficients == 4);
    CHECK(!sea.tide[0].high && sea.tide[1].high && close_to(sea.tide[7].height_m, 7.5f));
    CHECK(sea.coefficients[3].value == 73);

    // Bad items are skipped, the others kept; a coefficient out of 0..200 is not one.
    sign("SEA|Spot|Town|1794133609|1794132000|0|12.0|25.0|||||X:1794130000:1.0,H:abc:2.0,H:1794150000:,L:1794160000:-1.5|"
         "1794150000:0,1794160000:250,1794170000:90|ok|||\n",
         frame, sizeof(frame));
    CHECK(vigie_decode_sea(frame, sea));
    CHECK(sea.tide_count == 2 && sea.tide[0].high && !vigie_known(sea.tide[0].height_m) && !sea.tide[1].high);
    CHECK(sea.coefficient_count == 1 && sea.coefficients[0].value == 90);

    // Unknown tags and extra fields are skipped; a second SEA line is ignored; ACT before SEA is ignored.
    std::snprintf(body, sizeof(body),
                  "%sFUTURE|a|b\n"
                  "SEA|Spot|Town|1794133609|1794132000|0|12.0|25.0|||||||ok|||future-field|another\n"
                  "SEA|Other|Town|1794133700|1794132000|0|12.0|25.0|||||||ok|||\n"
                  "%s"
                  "CRIT|surf|swell_height|0.9|m|small|possible|1|2\n"
                  "TIDE_FUTURE|x\n"
                  "CRIT|kite|wind|10.0|kn|in_range|good||\n"  // another activity than the ACT line above: ignored
                  "CRIT|surf|swell_period|8.0|s|medium|possible|1|2\n",
                  act("boat", "1", "good", "", "", "", "", "", "", "", "").text,  // before SEA: ignored
                  act("surf", "1", "possible", "swell_height.small", "", "", "", "", "1794132000", "3", "8").text);
    sign(body, frame, sizeof(frame));
    CHECK(vigie_decode_sea(frame, sea));
    CHECK_STR(sea.name, "Spot");
    const VigieActivityForecast& surf = sea.activities[0];
    CHECK(surf.verdict == VigieVerdict::POSSIBLE && surf.points == 3 && surf.out_of == 8);
    CHECK(surf.criterion_count == 2 && surf.criteria[1].id == VigieCriterionId::SWELL_PERIOD);
    CHECK(!sea.activities[1].present && !sea.activities[2].present);

    // CRIT lines before any ACT, for another activity, or past the capacity are ignored; an unknown
    // criterion name is not counted but does not stop the next ones; the next ACT starts a new list.
    std::snprintf(body, sizeof(body), "%sCRIT|surf|swell_height|0.9|m|small|possible|1|2\n%sCRIT|surf|unheard_of|1.0|m|x|good||\n",
                  kSea, act("surf", "1", "possible", "swell_height.small", "", "", "", "", "1794132000", "3", "8").text);
    for (int i = 0; i < 8; ++i) std::strcat(body, "CRIT|surf|swell_period|8.0|s|medium|possible|1|2\n");
    std::strcat(body, act("boat", "1", "good", "", "", "", "", "", "1794132000", "", "").text);
    std::strcat(body, "CRIT|boat|sea_height|0.5|m|low|good||\n");
    sign(body, frame, sizeof(frame));
    CHECK(vigie_decode_sea(frame, sea));
    CHECK(sea.activities[0].criterion_count == kVigieMaxCriteria && kVigieMaxCriteria == 6);
    CHECK(sea.activities[0].criteria[0].id == VigieCriterionId::SWELL_PERIOD);
    CHECK(sea.activities[2].criterion_count == 1 && sea.activities[2].criteria[0].id == VigieCriterionId::SEA_HEIGHT);

    // A repeated ACT line replaces the first (no stale criteria stay behind).
    std::snprintf(body, sizeof(body), "%s%sCRIT|surf|swell_height|0.9|m|small|possible|1|2\n%s", kSea,
                  act("surf", "1", "possible", "swell_height.small", "", "", "", "", "1794132000", "3", "8").text,
                  act("surf", "1", "good", "", "", "", "", "", "1794132000", "", "").text);
    sign(body, frame, sizeof(frame));
    CHECK(vigie_decode_sea(frame, sea));
    CHECK(sea.activities[0].verdict == VigieVerdict::GOOD && sea.activities[0].criterion_count == 0);

    // A line longer than the contract's 512 bytes is cut, not overflowed (SEA name capped).
    char longname[600];
    std::memset(longname, 'a', sizeof(longname) - 1);
    longname[sizeof(longname) - 1] = '\0';
    std::snprintf(body, sizeof(body), "SEA|%s|Town|1794133609|1794132000|0|12.0|25.0|||||||ok|||\n", longname);
    sign(body, frame, sizeof(frame));
    VigieSea cut{};
    CHECK(!vigie_decode_sea(frame, cut));  // cut at 511 bytes, the columns after the name are lost: refused
}

// ─── Names and the contract's vocabulary ────────────────────────────────────

void test_names_and_vocabulary() {
    CHECK(vigie_activity_from_name("surf") == VigieActivity::SURF && vigie_activity_from_name("kite") == VigieActivity::KITE);
    CHECK(vigie_activity_from_name("boat") == VigieActivity::BOAT);
    CHECK(vigie_activity_from_name("shore_fishing") == VigieActivity::SHORE_FISHING);
    CHECK(vigie_activity_from_name("diving") == VigieActivity::COUNT_ && vigie_activity_from_name(nullptr) == VigieActivity::COUNT_);
    CHECK(vigie_verdict_from_name("good") == VigieVerdict::GOOD && vigie_verdict_from_name("poor") == VigieVerdict::POOR);
    CHECK(vigie_verdict_from_name("possible") == VigieVerdict::POSSIBLE && vigie_verdict_from_name("") == VigieVerdict::UNKNOWN);
    CHECK(vigie_verdict_from_name("great") == VigieVerdict::UNKNOWN);
    CHECK(vigie_unit_from_name("") == VigieUnit::NONE && vigie_unit_from_name("furlong") == VigieUnit::UNKNOWN);

    // Every name of the contract's table is read, with its unit.
    for (const VocabularyRow& row : kContractVocabulary) {
        CHECK(vigie_criterion_from_name(row.criterion) != VigieCriterionId::UNKNOWN);
        CHECK(vigie_unit_from_name(row.unit) != VigieUnit::UNKNOWN);
        for (int i = 0; i < 6 && row.states[i] != nullptr; ++i) {
            if (vigie_criterion_state_from_name(row.states[i]) == VigieCriterionState::UNKNOWN) {
                std::printf("FAIL unknown state %s.%s\n", row.criterion, row.states[i]);
                ++test_failures();
            }
        }
    }
    // The reasons of the vigilance cap.
    CHECK(vigie_criterion_from_name("vigilance") == VigieCriterionId::VIGILANCE);
    CHECK(vigie_criterion_state_from_name("orange") == VigieCriterionState::ORANGE);
    CHECK(vigie_criterion_state_from_name("red") == VigieCriterionState::RED);
    CHECK(vigie_criterion_from_name("") == VigieCriterionId::UNKNOWN && vigie_criterion_from_name("moon") == VigieCriterionId::UNKNOWN);

    // The reason of an ACT line, in both forms.
    VigieSea sea{};
    char frame[1024];
    char body[2048];
    std::snprintf(body, sizeof(body), "%s%s%s", kSea,
                  act("boat", "1", "possible", "vigilance.orange", "1794132000", "1794135600", "PPPPPPPPPPPPPPPPPPPPPPPP",
                      "111111111111111111111111", "1794132000", "", "").text,
                  act("kite", "1", "poor", "moon.full", "", "", "XXXXXXXXXXXXXXXXXXXXXXXX", "111111111111111111111111",
                      "1794132000", "", "").text);
    sign(body, frame, sizeof(frame));
    CHECK(vigie_decode_sea(frame, sea));
    CHECK(sea.activities[2].reason_criterion == VigieCriterionId::VIGILANCE && sea.activities[2].reason_state == VigieCriterionState::ORANGE);
    // A reason this firmware cannot name stays unknown; the verdict is kept.
    CHECK(sea.activities[1].verdict == VigieVerdict::POOR && sea.activities[1].reason_criterion == VigieCriterionId::UNKNOWN);
}

void test_spot_id() {
    CHECK(vigie_spot_id_valid("soorts-hossegor-plage-sud-point-d-or"));
    CHECK(vigie_spot_id_valid("a") && vigie_spot_id_valid("spot_1"));
    CHECK(!vigie_spot_id_valid(nullptr) && !vigie_spot_id_valid(""));
    CHECK(!vigie_spot_id_valid("Spot") && !vigie_spot_id_valid("a b") && !vigie_spot_id_valid("a&b=c"));
    CHECK(!vigie_spot_id_valid("a/b") && !vigie_spot_id_valid("é"));
    char sixty_five[66];
    std::memset(sixty_five, 'a', 65);
    sixty_five[65] = '\0';
    CHECK(!vigie_spot_id_valid(sixty_five) && vigie_spot_id_valid(sixty_five + 1));
}

// The tide curve of the sea page: a half cosine between the extremes the frame gives, nothing outside them.
void test_tide_height() {
    VigieSea sea{};
    CHECK(vigie_decode_sea(g_real, sea));
    CHECK(sea.tide_count == 6);
    // The real extremes: low -1.66 m at 1794126780, high 1.71 m at 1794149460.
    float h = 99.0f;
    CHECK(vigie_tide_height_at(sea, 1794126780u, h) && std::fabs(h - (-1.66f)) < 0.001f);
    CHECK(vigie_tide_height_at(sea, 1794149460u, h) && std::fabs(h - 1.71f) < 0.001f);
    // Half way in time: half way in height (the cosine is symmetric).
    CHECK(vigie_tide_height_at(sea, 1794138120u, h) && std::fabs(h - (-1.66f + 1.71f) / 2.0f) < 0.01f);
    // Rising between a low and the next high, never above or below them.
    float previous = -100.0f;
    for (uint32_t t = 1794126780u; t <= 1794149460u; t += 900u) {
        CHECK(vigie_tide_height_at(sea, t, h));
        CHECK(h >= previous && h >= -1.661f && h <= 1.711f);
        previous = h;
    }
    // Before the first extreme and after the last one: unknown, not extended.
    h = 99.0f;
    CHECK(!vigie_tide_height_at(sea, 1794126779u, h) && h == 99.0f);
    CHECK(!vigie_tide_height_at(sea, 1794238021u, h) && h == 99.0f);
    // An unknown height between two extremes: no curve there.
    VigieSea unknown = sea;
    unknown.tide[1].height_m = kVigieUnknown;
    CHECK(!vigie_tide_height_at(unknown, 1794138120u, h));
    // Extremes out of time order are not drawn.
    VigieSea shuffled = sea;
    shuffled.tide[1].utc = shuffled.tide[0].utc - 60u;
    CHECK(!vigie_tide_height_at(shuffled, 1794126800u, h));
    // No tide model near the spot: nothing, and one extreme alone is no curve.
    VigieSea none{};
    CHECK(!vigie_tide_height_at(none, 1794138120u, h));
    none.tide_count = 1;
    none.tide[0].utc = 1794126780u;
    none.tide[0].height_m = 1.0f;
    CHECK(!vigie_tide_height_at(none, 1794126780u, h));
}

// The coefficient of the next high water: the badge of the tide panel.
void test_coefficient_for() {
    VigieSea sea{};
    CHECK(vigie_decode_sea(g_real, sea));
    CHECK(sea.coefficient_count == 3);
    CHECK(vigie_sea_coefficient_for(sea, 1794132000u) == 0 && sea.coefficients[0].value == 83);
    CHECK(vigie_sea_coefficient_for(sea, 1794150900u) == 0);  // at the high water itself
    CHECK(vigie_sea_coefficient_for(sea, 1794150901u) == 1 && sea.coefficients[1].value == 84);
    CHECK(vigie_sea_coefficient_for(sea, 1794239401u) == -1);  // none still to come
    VigieSea unknown = sea;
    unknown.coefficients[0].value = 0;  // not known: skipped, never shown as 0
    CHECK(vigie_sea_coefficient_for(unknown, 1794132000u) == 1);
    CHECK(vigie_sea_coefficient_for(VigieSea{}, 1794132000u) == -1);
}

// The frame the CI renders with a coastal warning (the real frame plus an orange warning on its SEA line).
void test_warning_fixture(const char* path) {
    static char frame[32768];
    CHECK(read_file(path, frame, sizeof(frame)) > 0);
    VigieSea sea{};
    CHECK(vigie_decode_sea(frame, sea));
    CHECK(sea.warnings_known && sea.warning_present && sea.warning_level == VigieSeverity::ORANGE);
    CHECK(sea.warning_until_utc == 1794160800u);
    CHECK_STR(sea.warning_source, "Meteo-France");
    CHECK_STR(sea.name, "Plage Sud (Point d'Or)");
}

}  // namespace

int main(int argc, char** argv) {
    const char* path = argc > 2 ? argv[2] : "tests/fixtures/api_frame_activities.txt";
    const size_t size = read_file(path, g_real, sizeof(g_real));
    CHECK(size > 0);
    test_warning_fixture(argc > 3 ? argv[3] : "tests/fixtures/api_frame_activities_warning.txt");
    // The sea is on the decoder's stack and in a global: it stays small (868 bytes when written).
    CHECK(sizeof(VigieSea) <= 1024);
    if (size > 0) test_real_frame();
    test_freshness();
    test_update_keeps_the_sea();
    test_empty_fields_are_unknown();
    test_warnings();
    test_caps_and_order();
    test_names_and_vocabulary();
    test_spot_id();
    if (size > 0) {
        test_tide_height();
        test_coefficient_for();
    }
    return test_report("vigie_sea");
}
