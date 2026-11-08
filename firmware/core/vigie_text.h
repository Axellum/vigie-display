/**
 * @file vigie_text.h
 * @brief Everything the screen writes, computed without LVGL, in English or French.
 *
 * Screen language: English by default, French on request (substitution
 * `vigie_language: fr`). Every on-screen sentence goes through vigie_tr() or one of
 * the formatters below; numbers follow the language (6.3 / 6,3, 16,900 / 16 900).
 * Also holds the world map projection, shared with tools/make_world_map.py.
 *
 * Constraints: C++17, no allocation, no ESPHome nor LVGL; tested on a PC by
 * tools/test_vigie_text.cpp. Every formatter writes into a caller buffer with
 * snprintf and returns false (empty buffer) when it cannot.
 * When adding an MDI icon here, add its code point to the `glyphs:` of its font in
 * firmware/packages/vigie-screen.yaml: a missing glyph renders blank with no build error
 * (tests/test_firmware.py checks it).
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "vigie_model.h"
#include "vigie_sea.h"

// ─── Language ───────────────────────────────────────────────────────────────
enum class VigieLanguage : uint8_t { EN = 0, FR = 1 };

void vigie_set_language(VigieLanguage language);
VigieLanguage vigie_language();
// "fr" → FR, anything else → EN.
VigieLanguage vigie_language_from_code(const char* code);

enum class VigieText : uint8_t {
    ACTIVE_ALERTS,     // panel title
    NO_ACTIVE_ALERT,   // empty alert list, at least one hazard source answered
    ALERTS_UNKNOWN,    // no hazard source answered the API: not an all-clear
    WAITING_DATA,      // before the first frame
    NO_WEATHER,        // the API had no weather for the zone
    WIND,
    GUSTS,
    HUMIDITY,
    RAIN_NEXT_HOUR,
    AIR_QUALITY,
    WAVES,
    FEELS_LIKE,        // format: one %s (temperature)
    RAIN_NONE,
    RAIN_NOW,
    RAIN_IN_MIN,       // format: one %d (minutes)
    LINK_WAITING,
    LINK_UP_TO_DATE,   // format: one %s (age)
    LINK_DOWN,
    LINK_DOWN_SINCE,   // format: one %s (age)
    SEA_UNAVAILABLE,       // no sea data (yet, or none for this spot)
    SEA_AGE,               // format: one %s (age, "12 min ago"): sea data some minutes old
    SEA_PROVIDER_STALE,    // the API served the last answer of a provider
    SEA_EXPIRED,           // too old (or the 24 hours are over): the verdicts are not shown
    SEA_RULES_NOTICE,      // every verdict is a rule of Vigie, not an official one
    SEA_WARNINGS_UNKNOWN,  // the coastal warnings could not be read: not an all-clear
    SEA_WARNING,           // format: one %s (level)
    SEA_WARNING_UNTIL,     // format: two %s (level, time)
    SEA_NOT_AVAILABLE,     // an activity that cannot be judged here
    TIDE_HIGH,             // "High water"
    TIDE_LOW,              // "Low water"
    COEFFICIENT_ESTIMATED, // format: one %u (coefficient, estimated by Vigie)
    SEA_PAGE,              // the switch of the world page to the sea page
    WORLD_PAGE,            // the switch of the sea page back to the world page
    SEA_BEST_SLOT,         // above the best slot of an activity
    SEA_NO_SLOT,           // no good slot in the next 24 hours
    SEA_TIDE,              // title of the tide panel
    SEA_NO_TIDE,           // no tide model near the spot
    SEA_SUN,               // title of the sun panel
    SEA_SUNRISE,
    SEA_SUNSET,
    SEA_NIGHT,             // legend: the dark bars are the night hours
    SEA_LEGEND_BARS,       // legend: what a bar and the white line are
    SEA_SCORE,             // format: two %d (points, out of)
    SEA_DETAIL_TITLE,      // format: one %s (activity)
    SEA_DETAIL_FOR,        // format: one %s (time of the hour the verdict stands on)
    SEA_COL_CRITERION,     // column titles of the detail
    SEA_COL_VALUE,
    SEA_COL_SCORE,
    SEA_COL_READING,
    COUNT_
};

// Text of `id` in the current language (never null).
const char* vigie_tr(VigieText id);

// ─── World map (equirectangular, cropped) ───────────────────────────────────
// Antarctica and the far north almost never carry an alert: the map spans 60° S to
// 84° N, which leaves room for the alert list below it.
// Size in pixels: per board (`board_map_width` / `board_map_height` of its package, passed as
// build flags); the defaults are those of the Waveshare 7B. 360° × 144° = 2.5 : 1.
#ifndef VIGIE_MAP_WIDTH
#define VIGIE_MAP_WIDTH 664
#endif
#ifndef VIGIE_MAP_HEIGHT
#define VIGIE_MAP_HEIGHT 266
#endif
constexpr int32_t kVigieMapWidth = VIGIE_MAP_WIDTH;
constexpr int32_t kVigieMapHeight = VIGIE_MAP_HEIGHT;
constexpr double kVigieMapLatNorth = 84.0;
constexpr double kVigieMapLatSouth = -60.0;

struct VigiePixel {
    int32_t x = 0;
    int32_t y = 0;
    bool inside = false;  // false: latitude outside the band (clamped to the edge) or non-finite
};

VigiePixel vigie_map_project(double lat_deg, double lon_deg, int32_t width, int32_t height);

// ─── Labels and icons ───────────────────────────────────────────────────────
// WMO weather code (Open-Meteo) → short phrase: "Clear sky", "Overcast"…; "" when unknown.
const char* vigie_weather_label(uint16_t wmo_code);
// WMO weather code → MDI icon (UTF-8); `night` picks the moon for a clear sky; "" when unknown.
const char* vigie_weather_icon(uint16_t wmo_code, bool night);
const char* vigie_hazard_name(VigieHazardType type);
const char* vigie_hazard_icon(VigieHazardType type);
// European AQI band: "Good" … "Extremely poor"; "" when unknown.
const char* vigie_aqi_label(uint16_t aqi);

// ─── Numbers, distances, times ──────────────────────────────────────────────
// 6.26 with 1 decimal → "6.3" (EN) / "6,3" (FR); negative zero → "0.0".
bool vigie_format_decimal(float value, int decimals, char* buf, size_t size);
// Rounded distance with grouped thousands: "850 km", "16,900 km" / "16 900 km".
bool vigie_format_distance(float km, char* buf, size_t size);
// Age of an event: "just now", "12 min ago", "3 h ago", "2 d ago" (FR: "il y a 12 min"…).
// False when a time is unknown (0); a future timestamp (clock skew) counts as "just now".
bool vigie_format_age(uint32_t now_utc, uint32_t event_utc, char* buf, size_t size);
// Local time "HH:MM" at `offset_s` from UTC.
bool vigie_format_clock(uint32_t utc, int32_t offset_s, char* buf, size_t size);
// Local long date: "Tuesday, October 20" / "mardi 20 octobre" ("1er" on the 1st in French).
bool vigie_format_date(uint32_t utc, int32_t offset_s, char* buf, size_t size);
// Short day name of an ISO date "2026-10-20": "Tue" / "mar.". A label that is not an ISO
// date is copied as is.
bool vigie_format_day_short(const char* iso_date, char* buf, size_t size);
// "48.86° N · 2.35° E".
bool vigie_format_lat_lon(double lat_deg, double lon_deg, char* buf, size_t size);
// Main quantity of a hazard: "M 6.3" (earthquake), "Kp 7" (solar storm); false otherwise
// (the API contract does not define the others yet).
bool vigie_format_hazard_value(const VigieHazard& hazard, char* buf, size_t size);
// Night at the zone's local time (before 07:00 or from 20:00): for the weather icon.
bool vigie_is_night(uint32_t utc, int32_t offset_s);

// ─── Weather texts ──────────────────────────────────────────────────────────
// Each returns false (empty buffer) when its value is unknown: the screen then hides it.
// Rounded temperature: "13°", "-2°" ("0°" for -0.4).
bool vigie_format_temperature(float celsius, char* buf, size_t size);
// "22 km/h".
bool vigie_format_speed(float kmh, char* buf, size_t size);
// "10 km/h SW" ("10 km/h" when the direction is unknown).
bool vigie_format_wind(float kmh, uint16_t from_deg, char* buf, size_t size);
// "91%" / "91 %".
bool vigie_format_percent(uint8_t pct, char* buf, size_t size);
// Weather phrase and feels-like: "Overcast · feels like 12°" (either part alone if the other
// is unknown).
bool vigie_format_condition(const VigieZone& zone, char* buf, size_t size);
// Rain within the hour: "none", "now", "in 15 min".
bool vigie_format_rain_next_hour(const VigieRainNowcast& rain, char* buf, size_t size);
// "47 · Moderate".
bool vigie_format_air_quality(uint16_t aqi, char* buf, size_t size);
// "3.4 m · 12 s" ("3.4 m" when the period is unknown).
bool vigie_format_waves(const VigieMarine& marine, char* buf, size_t size);
// Rain of a forecast day: its probability "98%", else its amount "5.6 mm" when above 0.
bool vigie_format_day_rain(const VigieDayForecast& day, char* buf, size_t size);

// ─── Sea texts (activities of a surf spot, see vigie_sea.h) ─────────────────
// "Surf", "Kite / wing", "Boat", "Shore fishing"; "" for COUNT_.
const char* vigie_activity_name(VigieActivity activity);
// "Good", "Possible", "Not advised"; "" when unknown.
const char* vigie_verdict_label(VigieVerdict verdict);
// "Swell", "Wind", "Tide"…; "" when unknown.
const char* vigie_criterion_label(VigieCriterionId criterion);
// What a rule made of a value: "too small", "offshore", "a bit light"…; "" for a pair the
// contract does not list (the screen then shows nothing, it does not guess).
const char* vigie_criterion_state_text(VigieCriterionId criterion, VigieCriterionState state);
// Value with its unit: "2.6 m", "10 s", "21°", "19 km/h", "11.9 kn", "Force 3", "102 min", "91".
// False when the value or the unit is unknown.
bool vigie_format_criterion_value(const VigieCriterion& criterion, char* buf, size_t size);
// Why an activity is not good: "Swell: too small", "Coastal warning: red"; false when unknown.
bool vigie_format_reason(const VigieActivityForecast& activity, char* buf, size_t size);
// "14:00–16:00" at `offset_s` from UTC; false when there is no slot.
bool vigie_format_slot(uint32_t start_utc, uint32_t end_utc, int32_t offset_s, char* buf, size_t size);
// "High water 14:12 · 1.9 m" ("High water 14:12" when the height is unknown); false without a time.
bool vigie_format_tide_extreme(const VigieTideExtreme& extreme, int32_t offset_s, char* buf, size_t size);
// "Coefficient 91 (estimated)": the number is Vigie's estimate, not the SHOM's.
bool vigie_format_tide_coefficient(const VigieTideCoefficient& coefficient, char* buf, size_t size);
// The strongest coastal warning: "Orange coastal warning · until 14:00". When they could not be
// read: "Coastal warnings unavailable". False (nothing to say) when none stands.
bool vigie_format_warning(const VigieSea& sea, int32_t offset_s, char* buf, size_t size);
// Line about the age of the sea data (from vigie_sea_freshness()): "" and false when fresh.
bool vigie_format_sea_status(const VigieSea& sea, VigieSeaFreshness freshness, uint32_t now_utc, char* buf, size_t size);
// "Score 7 of 8" (FR "Note 7 sur 8"); false when unknown (negative, no maximum, more than the maximum).
bool vigie_format_score(int points, int out_of, char* buf, size_t size);
// "2/2", the points one criterion gave; false when unknown.
bool vigie_format_criterion_points(const VigieCriterion& criterion, char* buf, size_t size);
// "Surf · detail" (FR "Surf · détail").
bool vigie_format_detail_title(VigieActivity activity, char* buf, size_t size);
// "for 11:00" (FR "pour 11:00"), the hour the verdict of the detail stands on; false without a time.
bool vigie_format_detail_hour(uint32_t utc, int32_t offset_s, char* buf, size_t size);

// What the banner under the title bar of the sea page says, if anything.
enum class VigieSeaBanner : uint8_t {
    NONE = 0,             // nothing to say
    WARNING = 1,          // a coastal warning stands: its level is `sea.warning_level`
    WARNING_UNKNOWN = 2,  // the coastal warnings could not be read: never an all-clear
    STATUS = 3,           // no sea data, too old, or some minutes old
};
// The most important thing to say, in `buf`. Too old or no data wins over a warning that is no
// longer current; otherwise a warning (or the unreadable warnings) wins over the age.
VigieSeaBanner vigie_format_sea_banner(const VigieSea& sea, VigieSeaFreshness freshness, uint32_t now_utc,
                                       int32_t offset_s, char* buf, size_t size);

