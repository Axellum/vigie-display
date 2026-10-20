/**
 * @file vigie_model.h
 * @brief World state of the Vigie display and the pure logic around it.
 *
 * Holds what the screen shows (watched zones with their weather, active hazards) and
 * the logic that does not need the hardware: great-circle geometry, NMEA 0183
 * parsing, decoding of the API's embedded frame, CRC32 integrity, the NVS block
 * layout, alarm acknowledgement and the alarm decision.
 *
 * Constraints: C++17, no dynamic allocation, no ESPHome, ESP-IDF or LVGL. Built and
 * tested on a PC by tools/test_vigie_model.cpp. Headers include each other without a
 * folder ("vigie_model.h"): ESPHome copies every `includes:` file flat into its build.
 * The frame format is the contract with vigie-api: see docs/api-frame.md.
 */
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

// ─── Capacities ─────────────────────────────────────────────────────────────
constexpr int kVigieMaxZones = 10;
constexpr int kVigieMaxHazards = 30;
constexpr int kVigieMaxForecastDays = 10;
constexpr int kVigieRainSlots15m = 4;
constexpr int kVigieRainSlots5m = 12;
constexpr int kVigieMaxAcknowledged = 16;

constexpr size_t kVigieIdMax = 32;
constexpr size_t kVigieZoneNameMax = 48;
constexpr size_t kVigieHazardTitleMax = 96;
constexpr size_t kVigieSourcesMax = 48;
constexpr size_t kVigieDayLabelMax = 12;
// Unknown values. The API sends an empty field when it does not know a value; the display
// keeps it unknown and never shows it as a number (AGENTS.md rule 3).
constexpr float kVigieUnknown = std::numeric_limits<float>::quiet_NaN();
constexpr uint8_t kVigieUnknownPct = 255;      // a percentage
constexpr uint16_t kVigieUnknownCode = 0xFFFF;  // a WMO code, an AQI, a direction
inline bool vigie_known(float value) { return std::isfinite(value); }

// NVS block: magic "VIG1" and layout version (bump it when VigieWorldState changes).
constexpr uint32_t kVigieNvsMagic = 0x56494731UL;
constexpr uint16_t kVigieNvsVersion = 3;

// ─── Enumerations ───────────────────────────────────────────────────────────
enum class VigieHazardType : uint8_t {
    EARTHQUAKE = 0,
    TSUNAMI = 1,
    VOLCANO = 2,
    CYCLONE = 3,  // hurricane, typhoon, tropical cyclone
    TORNADO = 4,
    FLOOD = 5,
    EXTREME_RAIN = 6,
    WILDFIRE = 7,
    SOLAR_STORM = 8,
};

enum class VigieSeverity : uint8_t {
    GREEN = 0,
    YELLOW = 1,
    ORANGE = 2,
    RED = 3,
};

// NOAA geomagnetic storm scale, from the planetary Kp index.
enum class VigieSolarLevel : uint8_t {
    QUIET = 0,   // Kp < 4
    ACTIVE = 1,  // 4 <= Kp < 5
    G1_MINOR = 2,
    G2_MODERATE = 3,
    G3_STRONG = 4,
    G4_SEVERE = 5,
    G5_EXTREME = 6,
};

// ─── State ──────────────────────────────────────────────────────────────────
struct VigieGpsFix {
    bool valid = false;
    double lat_deg = 0.0;        // +N / -S
    double lon_deg = 0.0;        // +E / -W
    float speed_knots = 0.0f;    // speed over ground
    float course_deg = 0.0f;     // course over ground [0, 360)
    float altitude_m = 0.0f;     // above mean sea level
    uint8_t satellites = 0;
    float hdop = 99.9f;
    uint32_t utc_hhmmss = 0;     // e.g. 123519
};

struct VigieRainNowcast {
    bool known = false;                       // false: no nowcast for this zone (fields empty)
    bool expected = false;                    // rain now or within the hour (when known)
    int16_t starts_in_min = -1;               // 0 = raining now, -1 = no rain within 60 min
    float max_mm_h = 0.0f;
    float slots_15m[kVigieRainSlots15m] = {};  // mm/h for 0-15, 15-30, 30-45, 45-60 min
    float slots_5m[kVigieRainSlots5m] = {};    // mm/h every 5 min over the hour
};

struct VigieMarine {
    bool valid = false;  // the wave height is known (0 is a measured calm sea)
    float wave_height_m = kVigieUnknown;
    float wave_period_s = kVigieUnknown;
    uint16_t wave_direction_deg = kVigieUnknownCode;  // direction the waves come from
    float swell_height_m = kVigieUnknown;
    float current_knots = kVigieUnknown;
};

struct VigieDayForecast {
    char label[kVigieDayLabelMax] = "";  // as sent by the API (ISO date)
    uint16_t weather_code = kVigieUnknownCode;  // WMO code
    float tmin_c = kVigieUnknown;
    float tmax_c = kVigieUnknown;
    float rain_mm = kVigieUnknown;
    uint8_t rain_probability_pct = kVigieUnknownPct;
    float wind_max_kmh = kVigieUnknown;
    float uv_max = kVigieUnknown;
};

struct VigieZone {
    char id[kVigieIdMax] = "";
    char name[kVigieZoneNameMax] = "";
    double lat = 0.0;
    double lon = 0.0;
    int32_t utc_offset_s = 0;
    bool utc_offset_known = false;  // false: the clock of this zone is shown in UTC
    bool is_mobile_gps = false;     // zone "GPS": follows the receiver's position
    bool has_weather = false;       // false: the API had no weather for this zone (fields empty)

    float temp_c = kVigieUnknown;
    float feels_like_c = kVigieUnknown;
    uint8_t humidity_pct = kVigieUnknownPct;
    float pressure_hpa = kVigieUnknown;
    float wind_kmh = kVigieUnknown;
    float gust_kmh = kVigieUnknown;
    uint16_t wind_deg = kVigieUnknownCode;      // direction the wind comes from
    uint16_t weather_code = kVigieUnknownCode;  // WMO code
    float uv = kVigieUnknown;
    uint16_t aqi = kVigieUnknownCode;           // European AQI

    VigieRainNowcast rain{};
    VigieMarine marine{};

    uint8_t day_count = 0;
    VigieDayForecast days[kVigieMaxForecastDays] = {};
};

struct VigieHazard {
    char id[kVigieIdMax] = "";
    VigieHazardType type = VigieHazardType::EARTHQUAKE;
    VigieSeverity severity = VigieSeverity::GREEN;
    char title[kVigieHazardTitleMax] = "";
    char sources[kVigieSourcesMax] = "";  // e.g. "USGS+EMSC"
    uint32_t timestamp_utc = 0;
    double lat = 0.0;
    double lon = 0.0;
    float value = kVigieUnknown;  // magnitude, Kp… (depends on the type)
    float depth_km = kVigieUnknown;
    bool tsunami_warning = false;
    // Computed on the device, from the GPS fix or else the active zone:
    float distance_km = 0.0f;
    float bearing_deg = 0.0f;
    int16_t tsunami_eta_min = -1;  // -1 = not applicable
};

struct VigieWorldState {
    uint32_t updated_utc = 0;  // server time of the last frame

    // false: no hazard source answered the API. An empty list is then NOT an all-clear.
    bool hazards_known = false;
    // Active hazards by severity, all of them (HEAD), not only the HAZ lines received.
    uint16_t red_count = 0;
    uint16_t orange_count = 0;
    uint16_t yellow_count = 0;
    uint16_t green_count = 0;

    float kp = kVigieUnknown;
    VigieSolarLevel solar_level = VigieSolarLevel::QUIET;  // meaningful when kp is known

    VigieGpsFix gps{};

    uint8_t zone_count = 0;
    int8_t active_zone = 0;
    VigieZone zones[kVigieMaxZones] = {};

    uint8_t hazard_count = 0;
    VigieHazard hazards[kVigieMaxHazards] = {};  // most severe first

    // Ring of acknowledged hazard ids (no second alarm for the same hazard).
    uint8_t acknowledged_count = 0;
    uint8_t acknowledged_cursor = 0;
    char acknowledged[kVigieMaxAcknowledged][kVigieIdMax] = {};
};

struct VigieNvsBlock {
    uint32_t magic = 0;
    uint16_t version = 0;
    uint16_t state_size = 0;
    uint32_t crc32 = 0;
    VigieWorldState state{};
};

struct VigieAlarmDecision {
    bool trigger = false;
    bool wake_panel = false;
    bool sound = false;
    VigieSeverity severity = VigieSeverity::GREEN;
    int8_t hazard_index = -1;
    char hazard_id[kVigieIdMax] = "";
    float distance_km = 0.0f;
};

// ─── Geometry ───────────────────────────────────────────────────────────────
// Great-circle distance on the mean Earth sphere (R = 6371.0088 km); 0 for non-finite input.
double vigie_haversine_km(double lat1_deg, double lon1_deg, double lat2_deg, double lon2_deg);
// Initial true bearing from point 1 to point 2, in [0, 360).
double vigie_bearing_deg(double lat1_deg, double lon1_deg, double lat2_deg, double lon2_deg);
// 16-point compass rose: "N", "NNE"… in English; "O" for west and "SO", "NO"… in French.
const char* vigie_compass_point(double bearing_deg, bool french);

constexpr double vigie_km_to_nm(double km) { return km / 1.852; }
constexpr float vigie_knots_to_kmh(float kt) { return kt * 1.852f; }
constexpr float vigie_kmh_to_knots(float kmh) { return kmh / 1.852f; }

// "48°51.39'N 002°21.13'E" (degrees and decimal minutes, nautical style).
bool vigie_format_coord_dm(double lat_deg, double lon_deg, char* buf, size_t size);
// "48°51'23\"N 002°21'08\"E".
bool vigie_format_coord_dms(double lat_deg, double lon_deg, char* buf, size_t size);

// ─── NMEA 0183 ──────────────────────────────────────────────────────────────
// XOR checksum between '$' and '*' against the two hex digits after '*'.
bool vigie_nmea_checksum_ok(const char* sentence);
// Parses $xxGGA and $xxRMC into `fix`. False on a bad checksum or another sentence type.
bool vigie_parse_nmea(const char* sentence, VigieGpsFix& fix);

// ─── State updates ──────────────────────────────────────────────────────────
void vigie_reset_state(VigieWorldState& state);
// Distance, bearing and tsunami ETA of every hazard from the GPS fix, or else from the
// active zone; keeps the mobile GPS zone on the fix.
void vigie_update_distances(VigieWorldState& state);
void vigie_update_counts(VigieWorldState& state);
VigieSolarLevel vigie_solar_level_from_kp(float kp);
// Next / previous watched zone (wraps around), distances updated. Returns the new index.
int8_t vigie_next_zone(VigieWorldState& state);
int8_t vigie_previous_zone(VigieWorldState& state);

// ─── API frame ──────────────────────────────────────────────────────────────
// Checks the final "END|<CRC32 hex>" line: IEEE 802.3 CRC32 of every byte before "END|".
// A frame without an END line is accepted here (vigie_frame_complete() is the strict check).
bool vigie_frame_crc_ok(const char* frame);
// Decodes HEAD / ZONE / RAIN5M / DAY / HAZ lines into `state` (zones and hazards replaced,
// GPS fix and acknowledgements kept), sorts hazards, updates distances and counts (those of
// HEAD when it gives them). An empty field stays unknown (kVigieUnknown…), never 0.
// `state` is left untouched when the CRC is wrong. False if nothing could be read.
bool vigie_decode_frame(const char* frame, VigieWorldState& state);

// ─── Persistence and alarm ──────────────────────────────────────────────────
uint32_t vigie_crc32(const void* data, size_t size);
void vigie_nvs_pack(const VigieWorldState& state, VigieNvsBlock& block);
bool vigie_nvs_unpack(const VigieNvsBlock& block, VigieWorldState& state);

bool vigie_is_acknowledged(const VigieWorldState& state, const char* hazard_id);
bool vigie_acknowledge(VigieWorldState& state, const char* hazard_id);
// First hazard that is not acknowledged and is RED, or ORANGE with a tsunami warning or
// within `orange_radius_km` of the reference point.
VigieAlarmDecision vigie_evaluate_alarm(const VigieWorldState& state, float orange_radius_km = 1500.0f);

extern VigieWorldState g_vigie_state;
