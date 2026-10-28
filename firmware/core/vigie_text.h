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
