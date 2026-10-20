/**
 * @file vigie_model.cpp
 * @brief Implementation of vigie_model.h (see the header).
 */
#include "vigie_model.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

VigieWorldState g_vigie_state{};

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kRadToDeg = 180.0 / kPi;
constexpr double kEarthRadiusKm = 6371.0088;  // IUGG mean radius
// Mean deep-ocean tsunami speed, ~700 km/h. An order of magnitude, not a forecast.
constexpr double kTsunamiKmPerMin = 11.6666667;
constexpr size_t kFrameLineMax = 512;
constexpr int kFrameFieldsMax = 32;

constexpr const char* kCompassEn[16] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
                                        "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
constexpr const char* kCompassFr[16] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
                                        "S", "SSO", "SO", "OSO", "O", "ONO", "NO", "NNO"};

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

void copy_text(char* dst, size_t dst_size, const char* src) {
    if (dst == nullptr || dst_size == 0) return;
    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }
    std::strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
    // Do not leave half of a UTF-8 character at the end.
    size_t n = std::strlen(dst);
    size_t start = n;
    while (start > 0 && (static_cast<unsigned char>(dst[start - 1]) & 0xC0) == 0x80) --start;
    if (start > 0) {
        const unsigned char lead = static_cast<unsigned char>(dst[start - 1]);
        const size_t expected = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
        if (expected > 1 && n - (start - 1) < expected) dst[start - 1] = '\0';
    }
}

// Splits a writable string on `sep` in place (empty fields kept). Returns the field count.
int split_fields(char* text, char sep, char* fields[], int max_fields) {
    if (text == nullptr || max_fields <= 0) return 0;
    int count = 0;
    fields[count++] = text;
    for (char* p = text; *p != '\0'; ++p) {
        if (*p != sep) continue;
        *p = '\0';
        if (count >= max_fields) break;
        fields[count++] = p + 1;
    }
    return count;
}

// "ddmm.mmmm" (latitude) or "dddmm.mmmm" (longitude) plus hemisphere → signed degrees.
bool parse_nmea_coord(const char* value, const char* hemisphere, double max_abs, double& out_deg) {
    if (value == nullptr || value[0] == '\0' || hemisphere == nullptr || hemisphere[0] == '\0') return false;
    const char h = hemisphere[0];
    if (h != 'N' && h != 'S' && h != 'E' && h != 'W') return false;
    char* end = nullptr;
    const double raw = std::strtod(value, &end);
    if (end == value || !std::isfinite(raw) || raw < 0.0) return false;
    const int degrees = static_cast<int>(raw / 100.0);
    const double minutes = raw - static_cast<double>(degrees * 100);
    if (minutes < 0.0 || minutes >= 60.0) return false;
    double deg = static_cast<double>(degrees) + minutes / 60.0;
    if (deg > max_abs) return false;
    out_deg = (h == 'S' || h == 'W') ? -deg : deg;
    return true;
}

bool equals(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

VigieHazardType parse_hazard_type(const char* s) {
    if (s == nullptr || s[0] == '\0') return VigieHazardType::EARTHQUAKE;
    if (s[0] >= '0' && s[0] <= '8' && s[1] == '\0') return static_cast<VigieHazardType>(s[0] - '0');
    if (equals(s, "EARTHQUAKE") || equals(s, "EQ")) return VigieHazardType::EARTHQUAKE;
    if (equals(s, "TSUNAMI") || equals(s, "TS")) return VigieHazardType::TSUNAMI;
    if (equals(s, "VOLCANO") || equals(s, "VO")) return VigieHazardType::VOLCANO;
    if (equals(s, "HURRICANE") || equals(s, "CYCLONE") || equals(s, "TYPHOON") || equals(s, "TC")) {
        return VigieHazardType::CYCLONE;
    }
    if (equals(s, "TORNADO")) return VigieHazardType::TORNADO;
    if (equals(s, "FLOOD") || equals(s, "FL")) return VigieHazardType::FLOOD;
    if (equals(s, "EXTREME_RAIN") || equals(s, "RAIN")) return VigieHazardType::EXTREME_RAIN;
    if (equals(s, "WILDFIRE") || equals(s, "FIRE") || equals(s, "WF")) return VigieHazardType::WILDFIRE;
    if (equals(s, "SOLAR_STORM") || equals(s, "SOLAR")) return VigieHazardType::SOLAR_STORM;
    return VigieHazardType::EARTHQUAKE;
}

VigieSeverity parse_severity(const char* s) {
    if (s == nullptr || s[0] == '\0') return VigieSeverity::GREEN;
    if (s[0] >= '0' && s[0] <= '3' && s[1] == '\0') return static_cast<VigieSeverity>(s[0] - '0');
    if (equals(s, "RED") || equals(s, "EXTREME")) return VigieSeverity::RED;
    if (equals(s, "ORANGE") || equals(s, "SEVERE")) return VigieSeverity::ORANGE;
    if (equals(s, "YELLOW") || equals(s, "MODERATE")) return VigieSeverity::YELLOW;
    return VigieSeverity::GREEN;
}

// Most severe first; then tsunami warnings; then the largest value.
bool ranks_before(const VigieHazard& a, const VigieHazard& b) {
    if (a.severity != b.severity) return static_cast<uint8_t>(a.severity) > static_cast<uint8_t>(b.severity);
    if (a.tsunami_warning != b.tsunami_warning) return a.tsunami_warning;
    return a.value > b.value;
}

void sort_hazards(VigieWorldState& state) {
    const int n = state.hazard_count <= kVigieMaxHazards ? state.hazard_count : kVigieMaxHazards;
    for (int i = 1; i < n; ++i) {  // insertion sort: stable, at most 30 items
        VigieHazard item = state.hazards[i];
        int j = i - 1;
        while (j >= 0 && ranks_before(item, state.hazards[j])) {
            state.hazards[j + 1] = state.hazards[j];
            --j;
        }
        state.hazards[j + 1] = item;
    }
}

int compass_index(double bearing_deg) {
    double norm = std::fmod(bearing_deg, 360.0);
    if (norm < 0.0) norm += 360.0;
    return static_cast<int>(std::floor((norm + 11.25) / 22.5)) & 15;
}

float read_float(const char* field, float fallback) {
    if (field == nullptr || field[0] == '\0') return fallback;
    const float v = std::strtof(field, nullptr);
    return std::isfinite(v) ? v : fallback;
}

}  // namespace

// ─── Geometry ───────────────────────────────────────────────────────────────

double vigie_haversine_km(double lat1_deg, double lon1_deg, double lat2_deg, double lon2_deg) {
    if (!std::isfinite(lat1_deg) || !std::isfinite(lon1_deg) || !std::isfinite(lat2_deg) ||
        !std::isfinite(lon2_deg)) {
        return 0.0;
    }
    const double dlat = (lat2_deg - lat1_deg) * kDegToRad;
    const double dlon = (lon2_deg - lon1_deg) * kDegToRad;
    const double s_lat = std::sin(dlat * 0.5);
    const double s_lon = std::sin(dlon * 0.5);
    double a = s_lat * s_lat + std::cos(lat1_deg * kDegToRad) * std::cos(lat2_deg * kDegToRad) * s_lon * s_lon;
    if (a < 0.0) a = 0.0;
    if (a > 1.0) a = 1.0;
    return kEarthRadiusKm * 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
}

double vigie_bearing_deg(double lat1_deg, double lon1_deg, double lat2_deg, double lon2_deg) {
    if (!std::isfinite(lat1_deg) || !std::isfinite(lon1_deg) || !std::isfinite(lat2_deg) ||
        !std::isfinite(lon2_deg)) {
        return 0.0;
    }
    const double lat1 = lat1_deg * kDegToRad;
    const double lat2 = lat2_deg * kDegToRad;
    const double dlon = (lon2_deg - lon1_deg) * kDegToRad;
    const double y = std::sin(dlon) * std::cos(lat2);
    const double x = std::cos(lat1) * std::sin(lat2) - std::sin(lat1) * std::cos(lat2) * std::cos(dlon);
    if (std::fabs(x) < 1e-12 && std::fabs(y) < 1e-12) return 0.0;
    double bearing = std::fmod(std::atan2(y, x) * kRadToDeg + 360.0, 360.0);
    if (bearing < 0.0) bearing += 360.0;
    if (bearing >= 360.0) bearing -= 360.0;
    return bearing;
}

const char* vigie_compass_point(double bearing_deg, bool french) {
    if (!std::isfinite(bearing_deg)) return "N";
    const int i = compass_index(bearing_deg);
    return french ? kCompassFr[i] : kCompassEn[i];
}

bool vigie_format_coord_dm(double lat_deg, double lon_deg, char* buf, size_t size) {
    if (buf == nullptr || size < 24 || !std::isfinite(lat_deg) || !std::isfinite(lon_deg)) {
        if (buf != nullptr && size > 0) buf[0] = '\0';
        return false;
    }
    lat_deg = lat_deg < -90.0 ? -90.0 : (lat_deg > 90.0 ? 90.0 : lat_deg);
    lon_deg = lon_deg < -180.0 ? -180.0 : (lon_deg > 180.0 ? 180.0 : lon_deg);
    const auto split = [](double value, int& deg, double& minutes) {
        const double a = std::fabs(value);
        deg = static_cast<int>(std::floor(a));
        long hundredths = std::lround((a - deg) * 6000.0);
        if (hundredths >= 6000) {
            hundredths -= 6000;
            ++deg;
        }
        minutes = static_cast<double>(hundredths) / 100.0;
    };
    int dlat = 0, dlon = 0;
    double mlat = 0.0, mlon = 0.0;
    split(lat_deg, dlat, mlat);
    split(lon_deg, dlon, mlon);
    const int n = std::snprintf(buf, size, "%02d\xC2\xB0%05.2f'%c %03d\xC2\xB0%05.2f'%c", dlat, mlat,
                                lat_deg >= 0.0 ? 'N' : 'S', dlon, mlon, lon_deg >= 0.0 ? 'E' : 'W');
    return n > 0 && static_cast<size_t>(n) < size;
}

bool vigie_format_coord_dms(double lat_deg, double lon_deg, char* buf, size_t size) {
    if (buf == nullptr || size < 26 || !std::isfinite(lat_deg) || !std::isfinite(lon_deg)) {
        if (buf != nullptr && size > 0) buf[0] = '\0';
        return false;
    }
    lat_deg = lat_deg < -90.0 ? -90.0 : (lat_deg > 90.0 ? 90.0 : lat_deg);
    lon_deg = lon_deg < -180.0 ? -180.0 : (lon_deg > 180.0 ? 180.0 : lon_deg);
    const long slat = std::lround(std::fabs(lat_deg) * 3600.0);
    const long slon = std::lround(std::fabs(lon_deg) * 3600.0);
    const int n = std::snprintf(buf, size, "%02ld\xC2\xB0%02ld'%02ld\"%c %03ld\xC2\xB0%02ld'%02ld\"%c", slat / 3600,
                                (slat % 3600) / 60, slat % 60, lat_deg >= 0.0 ? 'N' : 'S', slon / 3600,
                                (slon % 3600) / 60, slon % 60, lon_deg >= 0.0 ? 'E' : 'W');
    return n > 0 && static_cast<size_t>(n) < size;
}

// ─── NMEA 0183 ──────────────────────────────────────────────────────────────

bool vigie_nmea_checksum_ok(const char* s) {
    if (s == nullptr) return false;
    while (*s == ' ' || *s == '\t') ++s;
    if (*s != '$') return false;
    ++s;
    uint8_t sum = 0;
    while (*s != '\0' && *s != '*' && *s != '\r' && *s != '\n') sum ^= static_cast<uint8_t>(*s++);
    if (*s != '*') return false;
    const int hi = hex_digit(s[1]);
    const int lo = hex_digit(s[2]);
    if (hi < 0 || lo < 0) return false;
    const char after = s[3];
    if (after != '\0' && after != '\r' && after != '\n' && after != ' ') return false;
    return sum == static_cast<uint8_t>((hi << 4) | lo);
}

bool vigie_parse_nmea(const char* sentence, VigieGpsFix& fix) {
    if (!vigie_nmea_checksum_ok(sentence)) return false;
    while (*sentence == ' ' || *sentence == '\t') ++sentence;
    const char* body = sentence + 1;
    const char* star = std::strchr(body, '*');
    if (star == nullptr) return false;
    char line[128];
    const size_t len = static_cast<size_t>(star - body);
    if (len < 6 || len >= sizeof(line)) return false;
    std::memcpy(line, body, len);
    line[len] = '\0';

    char* f[20] = {};
    const int n = split_fields(line, ',', f, 20);
    if (n < 1 || std::strlen(f[0]) != 5) return false;
    const char* type = f[0] + 2;  // talker id ignored: GP, GN, GL, BD, GA…

    if (equals(type, "GGA")) {
        if (n < 10) return false;
        if (f[1][0] != '\0') fix.utc_hhmmss = static_cast<uint32_t>(std::strtoul(f[1], nullptr, 10));
        fix.satellites = static_cast<uint8_t>(std::atoi(f[7]));
        const float hdop = read_float(f[8], -1.0f);
        if (hdop >= 0.0f) fix.hdop = hdop;
        fix.altitude_m = read_float(f[9], fix.altitude_m);
        if (std::atoi(f[6]) <= 0) {  // fix quality 0 = no fix
            fix.valid = false;
            return true;
        }
        double lat = 0.0, lon = 0.0;
        if (!parse_nmea_coord(f[2], f[3], 90.0, lat) || !parse_nmea_coord(f[4], f[5], 180.0, lon)) {
            fix.valid = false;
            return false;
        }
        fix.lat_deg = lat;
        fix.lon_deg = lon;
        fix.valid = true;
        return true;
    }

    if (equals(type, "RMC")) {
        if (n < 9) return false;
        if (f[1][0] != '\0') fix.utc_hhmmss = static_cast<uint32_t>(std::strtoul(f[1], nullptr, 10));
        if (f[2][0] != 'A') {  // V = void: a valid sentence, no fix
            fix.valid = false;
            return f[2][0] == 'V';
        }
        double lat = 0.0, lon = 0.0;
        if (!parse_nmea_coord(f[3], f[4], 90.0, lat) || !parse_nmea_coord(f[5], f[6], 180.0, lon)) {
            fix.valid = false;
            return false;
        }
        fix.lat_deg = lat;
        fix.lon_deg = lon;
        fix.valid = true;
        const float sog = read_float(f[7], -1.0f);
        if (sog >= 0.0f) fix.speed_knots = sog;
        if (f[8][0] != '\0') {
            float cog = std::fmod(read_float(f[8], 0.0f), 360.0f);
            if (cog < 0.0f) cog += 360.0f;
            fix.course_deg = cog;
        }
        return true;
    }
    return false;
}

// ─── State updates ──────────────────────────────────────────────────────────

void vigie_reset_state(VigieWorldState& state) { state = VigieWorldState{}; }

void vigie_update_distances(VigieWorldState& state) {
    double ref_lat = 0.0, ref_lon = 0.0;
    if (state.gps.valid) {
        ref_lat = state.gps.lat_deg;
        ref_lon = state.gps.lon_deg;
        if (state.zone_count > 0 && state.zones[0].is_mobile_gps) {
            state.zones[0].lat = ref_lat;
            state.zones[0].lon = ref_lon;
        }
    } else if (state.active_zone >= 0 && state.active_zone < static_cast<int8_t>(state.zone_count)) {
        ref_lat = state.zones[state.active_zone].lat;
        ref_lon = state.zones[state.active_zone].lon;
    } else {
        return;
    }
    const int n = state.hazard_count <= kVigieMaxHazards ? state.hazard_count : kVigieMaxHazards;
    for (int i = 0; i < n; ++i) {
        VigieHazard& h = state.hazards[i];
        const double d = vigie_haversine_km(ref_lat, ref_lon, h.lat, h.lon);
        h.distance_km = static_cast<float>(d);
        h.bearing_deg = static_cast<float>(vigie_bearing_deg(ref_lat, ref_lon, h.lat, h.lon));
        if (h.tsunami_warning || h.type == VigieHazardType::TSUNAMI) {
            const long eta = std::lround(d / kTsunamiKmPerMin);
            h.tsunami_eta_min = static_cast<int16_t>(eta > 32767 ? 32767 : (eta < 1 ? 1 : eta));
        } else {
            h.tsunami_eta_min = -1;
        }
    }
}

void vigie_update_counts(VigieWorldState& state) {
    state.red_count = state.orange_count = state.yellow_count = state.green_count = 0;
    const int n = state.hazard_count <= kVigieMaxHazards ? state.hazard_count : kVigieMaxHazards;
    for (int i = 0; i < n; ++i) {
        switch (state.hazards[i].severity) {
            case VigieSeverity::RED: ++state.red_count; break;
            case VigieSeverity::ORANGE: ++state.orange_count; break;
            case VigieSeverity::YELLOW: ++state.yellow_count; break;
            case VigieSeverity::GREEN: ++state.green_count; break;
        }
    }
}

VigieSolarLevel vigie_solar_level_from_kp(float kp) {
    if (!std::isfinite(kp) || kp < 4.0f) return VigieSolarLevel::QUIET;
    if (kp < 5.0f) return VigieSolarLevel::ACTIVE;
    if (kp < 6.0f) return VigieSolarLevel::G1_MINOR;
    if (kp < 7.0f) return VigieSolarLevel::G2_MODERATE;
    if (kp < 8.0f) return VigieSolarLevel::G3_STRONG;
    if (kp < 9.0f) return VigieSolarLevel::G4_SEVERE;
    return VigieSolarLevel::G5_EXTREME;
}

int8_t vigie_next_zone(VigieWorldState& state) {
    const int n = state.zone_count <= kVigieMaxZones ? state.zone_count : kVigieMaxZones;
    const int cur = state.active_zone;
    state.active_zone = static_cast<int8_t>((n == 0 || cur < 0 || cur >= n) ? 0 : (cur + 1) % n);
    vigie_update_distances(state);
    return state.active_zone;
}

int8_t vigie_previous_zone(VigieWorldState& state) {
    const int n = state.zone_count <= kVigieMaxZones ? state.zone_count : kVigieMaxZones;
    const int cur = state.active_zone;
    state.active_zone = static_cast<int8_t>((n == 0 || cur < 0 || cur >= n) ? 0 : (cur + n - 1) % n);
    vigie_update_distances(state);
    return state.active_zone;
}

// ─── API frame ──────────────────────────────────────────────────────────────

bool vigie_frame_crc_ok(const char* frame) {
    if (frame == nullptr || frame[0] == '\0') return false;
    const char* p = frame;
    while (*p != '\0') {
        const char* line = p;
        while (*line == ' ' || *line == '\t') ++line;
        if (std::strncmp(line, "END", 3) == 0 &&
            (line[3] == '|' || line[3] == '\0' || line[3] == '\r' || line[3] == '\n')) {
            if (line[3] != '|') return false;
            uint32_t expected = 0;
            for (int i = 0; i < 8; ++i) {
                const int d = hex_digit(line[4 + i]);
                if (d < 0) return false;
                expected = (expected << 4) | static_cast<uint32_t>(d);
            }
            const char after = line[12];
            if (after != '\0' && after != '\r' && after != '\n' && after != ' ' && after != '\t') return false;
            return vigie_crc32(frame, static_cast<size_t>(p - frame)) == expected;
        }
        while (*p != '\0' && *p != '\n') ++p;
        if (*p == '\n') ++p;
    }
    return true;  // no END line: unsigned frame (tests, older API)
}

bool vigie_decode_frame(const char* frame, VigieWorldState& state) {
    if (frame == nullptr || frame[0] == '\0' || !vigie_frame_crc_ok(frame)) return false;

    bool any = false;
    const char* cursor = frame;
    while (*cursor != '\0') {
        while (*cursor == '\r' || *cursor == '\n' || *cursor == ' ' || *cursor == '\t') ++cursor;
        if (*cursor == '\0') break;
        const char* end = cursor;
        while (*end != '\0' && *end != '\r' && *end != '\n') ++end;
        const size_t len = static_cast<size_t>(end - cursor);
        const char* start = cursor;
        cursor = end;
        if (len == 0 || *start == '#') continue;

        char line[kFrameLineMax];
        const size_t copy = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
        std::memcpy(line, start, copy);
        line[copy] = '\0';
        char* f[kFrameFieldsMax] = {};
        const int n = split_fields(line, '|', f, kFrameFieldsMax);
        if (n < 2) continue;
        const char* tag = f[0];

        if (equals(tag, "END")) break;

        if (equals(tag, "HEAD")) {
            // HEAD|<server_utc>|<kp>|<active_zone>|...
            state.updated_utc = static_cast<uint32_t>(std::strtoul(f[1], nullptr, 10));
            if (n > 2 && f[2][0] != '\0') {
                state.kp = read_float(f[2], 0.0f);
                state.solar_level = vigie_solar_level_from_kp(state.kp);
            }
            if (n > 3 && f[3][0] != '\0') {
                const int z = std::atoi(f[3]);
                if (z >= 0 && z < kVigieMaxZones) state.active_zone = static_cast<int8_t>(z);
            }
            state.zone_count = 0;
            state.hazard_count = 0;
            any = true;
            continue;
        }

        if (equals(tag, "ZONE")) {
            // ZONE|id|name|lat|lon|utc_offset_s|temp|feels_like|humidity|pressure|wind|gust|wind_deg|
            //      weather_code|uv|aqi|rain_starts_in_min|rain_max_mm_h|rain_15m_csv|wave_m|wave_period_s|
            //      wave_dir_deg|swell_m|current_kt
            if (n < 5 || state.zone_count >= kVigieMaxZones) continue;
            VigieZone& z = state.zones[state.zone_count++];
            z = VigieZone{};
            copy_text(z.id, sizeof(z.id), f[1]);
            copy_text(z.name, sizeof(z.name), f[2]);
            z.lat = std::strtod(f[3], nullptr);
            z.lon = std::strtod(f[4], nullptr);
            z.is_mobile_gps = equals(z.id, "GPS");
            if (n > 5 && f[5][0] != '\0') z.utc_offset_s = static_cast<int32_t>(std::strtol(f[5], nullptr, 10));
            z.has_weather = n > 6 && f[6][0] != '\0';
            if (n > 6) z.temp_c = read_float(f[6], 0.0f);
            if (n > 7) z.feels_like_c = read_float(f[7], z.temp_c);
            if (n > 8 && f[8][0] != '\0') z.humidity_pct = static_cast<uint8_t>(std::atoi(f[8]));
            if (n > 9) z.pressure_hpa = read_float(f[9], z.pressure_hpa);
            if (n > 10) z.wind_kmh = read_float(f[10], 0.0f);
            if (n > 11) z.gust_kmh = read_float(f[11], 0.0f);
            if (n > 12 && f[12][0] != '\0') z.wind_deg = static_cast<uint16_t>(std::atoi(f[12]));
            if (n > 13 && f[13][0] != '\0') z.weather_code = static_cast<uint16_t>(std::atoi(f[13]));
            if (n > 14) z.uv = read_float(f[14], 0.0f);
            if (n > 15 && f[15][0] != '\0') z.aqi = static_cast<uint16_t>(std::atoi(f[15]));
            if (n > 16 && f[16][0] != '\0') {
                z.rain.starts_in_min = static_cast<int16_t>(std::atoi(f[16]));
                z.rain.expected = z.rain.starts_in_min >= 0;
            }
            if (n > 17) {
                z.rain.max_mm_h = read_float(f[17], 0.0f);
                if (z.rain.max_mm_h > 0.0f) z.rain.expected = true;
            }
            if (n > 18 && f[18][0] != '\0') {
                char* slots[kVigieRainSlots15m] = {};
                const int k = split_fields(f[18], ',', slots, kVigieRainSlots15m);
                for (int i = 0; i < k; ++i) z.rain.slots_15m[i] = read_float(slots[i], 0.0f);
            }
            if (n > 19 && f[19][0] != '\0') {
                z.marine.wave_height_m = read_float(f[19], 0.0f);
                z.marine.valid = z.marine.wave_height_m > 0.0f;
            }
            if (n > 20) z.marine.wave_period_s = read_float(f[20], 0.0f);
            if (n > 21 && f[21][0] != '\0') z.marine.wave_direction_deg = static_cast<uint16_t>(std::atoi(f[21]));
            if (n > 22) z.marine.swell_height_m = read_float(f[22], 0.0f);
            if (n > 23) z.marine.current_knots = read_float(f[23], 0.0f);
            any = true;
            continue;
        }

        if (equals(tag, "RAIN5M")) {
            // RAIN5M|zone_id|v0,...,v11
            if (n < 3) continue;
            for (uint8_t i = 0; i < state.zone_count; ++i) {
                if (!equals(state.zones[i].id, f[1])) continue;
                char* slots[kVigieRainSlots5m] = {};
                const int k = split_fields(f[2], ',', slots, kVigieRainSlots5m);
                for (int j = 0; j < k; ++j) state.zones[i].rain.slots_5m[j] = read_float(slots[j], 0.0f);
                any = true;
                break;
            }
            continue;
        }

        if (equals(tag, "DAY")) {
            // DAY|zone_id|label|weather_code|tmin|tmax|rain_mm|rain_probability_pct|wind_max_kmh|uv_max
            if (n < 6) continue;
            for (uint8_t i = 0; i < state.zone_count; ++i) {
                VigieZone& z = state.zones[i];
                if (!equals(z.id, f[1]) || z.day_count >= kVigieMaxForecastDays) continue;
                VigieDayForecast& d = z.days[z.day_count++];
                copy_text(d.label, sizeof(d.label), f[2]);
                d.weather_code = static_cast<uint16_t>(std::atoi(f[3]));
                d.tmin_c = read_float(f[4], 0.0f);
                d.tmax_c = read_float(f[5], 0.0f);
                if (n > 6) d.rain_mm = read_float(f[6], 0.0f);
                if (n > 7 && f[7][0] != '\0') {
                    const int pct = std::atoi(f[7]);
                    d.rain_probability_pct = static_cast<uint8_t>(pct < 0 ? 0 : (pct > 100 ? 100 : pct));
                }
                if (n > 8) d.wind_max_kmh = read_float(f[8], 0.0f);
                if (n > 9) d.uv_max = read_float(f[9], 0.0f);
                any = true;
                break;
            }
            continue;
        }

        if (equals(tag, "HAZ")) {
            // HAZ|id|type|severity|lat|lon|value|depth_km|tsunami_0_1|timestamp_utc|sources|title
            if (n < 7 || state.hazard_count >= kVigieMaxHazards) continue;
            VigieHazard& h = state.hazards[state.hazard_count++];
            h = VigieHazard{};
            copy_text(h.id, sizeof(h.id), f[1]);
            h.type = parse_hazard_type(f[2]);
            h.severity = parse_severity(f[3]);
            h.lat = std::strtod(f[4], nullptr);
            h.lon = std::strtod(f[5], nullptr);
            h.value = read_float(f[6], 0.0f);
            if (n > 7) h.depth_km = read_float(f[7], 0.0f);
            if (n > 8) h.tsunami_warning = f[8][0] == '1' || f[8][0] == 'T' || f[8][0] == 't';
            if (n > 9 && f[9][0] != '\0') h.timestamp_utc = static_cast<uint32_t>(std::strtoul(f[9], nullptr, 10));
            if (n > 10) copy_text(h.sources, sizeof(h.sources), f[10]);
            if (n > 11) copy_text(h.title, sizeof(h.title), f[11]);
            any = true;
            continue;
        }
    }

    if (any) {
        sort_hazards(state);
        vigie_update_distances(state);
        vigie_update_counts(state);
    }
    return any;
}

// ─── Persistence and alarm ──────────────────────────────────────────────────

uint32_t vigie_crc32(const void* data, size_t size) {
    if (data == nullptr || size == 0) return 0;
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1UL)));
    }
    return ~crc;
}

void vigie_nvs_pack(const VigieWorldState& state, VigieNvsBlock& block) {
    block.magic = kVigieNvsMagic;
    block.version = kVigieNvsVersion;
    block.state_size = static_cast<uint16_t>(sizeof(VigieWorldState));
    block.state = state;
    block.crc32 = vigie_crc32(&block.state, sizeof(VigieWorldState));
}

bool vigie_nvs_unpack(const VigieNvsBlock& block, VigieWorldState& state) {
    if (block.magic != kVigieNvsMagic || block.version != kVigieNvsVersion ||
        block.state_size != static_cast<uint16_t>(sizeof(VigieWorldState))) {
        return false;
    }
    if (vigie_crc32(&block.state, sizeof(VigieWorldState)) != block.crc32) return false;
    state = block.state;
    return true;
}

bool vigie_is_acknowledged(const VigieWorldState& state, const char* hazard_id) {
    if (hazard_id == nullptr || hazard_id[0] == '\0') return false;
    const int n = state.acknowledged_count <= kVigieMaxAcknowledged ? state.acknowledged_count : kVigieMaxAcknowledged;
    for (int i = 0; i < n; ++i) {
        if (equals(state.acknowledged[i], hazard_id)) return true;
    }
    return false;
}

bool vigie_acknowledge(VigieWorldState& state, const char* hazard_id) {
    if (hazard_id == nullptr || hazard_id[0] == '\0') return false;
    if (vigie_is_acknowledged(state, hazard_id)) return true;
    const uint8_t slot = state.acknowledged_cursor % kVigieMaxAcknowledged;
    copy_text(state.acknowledged[slot], kVigieIdMax, hazard_id);
    state.acknowledged_cursor = static_cast<uint8_t>((slot + 1) % kVigieMaxAcknowledged);
    if (state.acknowledged_count < kVigieMaxAcknowledged) ++state.acknowledged_count;
    return true;
}

VigieAlarmDecision vigie_evaluate_alarm(const VigieWorldState& state, float orange_radius_km) {
    VigieAlarmDecision decision{};
    const int n = state.hazard_count <= kVigieMaxHazards ? state.hazard_count : kVigieMaxHazards;
    for (int i = 0; i < n; ++i) {
        const VigieHazard& h = state.hazards[i];
        if (h.id[0] == '\0' || vigie_is_acknowledged(state, h.id)) continue;
        const bool critical =
            h.severity == VigieSeverity::RED ||
            (h.severity == VigieSeverity::ORANGE &&
             (h.tsunami_warning || (h.distance_km > 0.0f && h.distance_km <= orange_radius_km)));
        if (!critical) continue;
        decision.trigger = decision.wake_panel = decision.sound = true;
        decision.severity = h.severity;
        decision.hazard_index = static_cast<int8_t>(i);
        copy_text(decision.hazard_id, sizeof(decision.hazard_id), h.id);
        decision.distance_km = h.distance_km;
        return decision;
    }
    return decision;
}
