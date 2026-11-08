/**
 * @file vigie_sea.cpp
 * @brief Implementation of vigie_sea.h (see the header).
 */
#include "vigie_sea.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace {

constexpr size_t kLineMax = 512;  // the contract: a line fits 512 bytes
constexpr int kFieldsMax = 32;
constexpr int kSeaFields = 18;
constexpr int kActFields = 12;
constexpr int kCritFields = 9;

// Names of the contract, in the order of the enums (checked by the static_asserts below).
constexpr const char* kCriterionNames[] = {
    "", "swell_height", "swell_period", "swell_direction", "wind", "tide", "gust_gap", "wind_direction",
    "wind_force", "gust_force", "sea_height", "low_water", "coefficient", "vigilance",
};
static_assert(sizeof(kCriterionNames) / sizeof(kCriterionNames[0]) == static_cast<size_t>(VigieCriterionId::COUNT_),
              "kCriterionNames must have one name per VigieCriterionId");

constexpr const char* kStateNames[] = {
    "", "too_small", "small", "right", "big", "too_big", "short", "medium", "long", "facing", "oblique", "off_axis",
    "light", "offshore", "cross_shore", "onshore", "strong", "right_stage", "wrong_stage", "in_range", "a_bit_light",
    "a_bit_strong", "too_light", "too_strong", "steady", "gusty", "very_gusty", "up_to_moderate", "fresh",
    "strong_or_more", "up_to_fresh", "gale_or_more", "near_gale_or_more", "low", "moderate", "high", "in_window",
    "outside_window", "large", "very_large", "orange", "red",
};
static_assert(sizeof(kStateNames) / sizeof(kStateNames[0]) == static_cast<size_t>(VigieCriterionState::COUNT_),
              "kStateNames must have one name per VigieCriterionState");

constexpr const char* kActivityNames[] = {"surf", "kite", "boat", "shore_fishing"};
static_assert(sizeof(kActivityNames) / sizeof(kActivityNames[0]) == static_cast<size_t>(VigieActivity::COUNT_),
              "kActivityNames must have one name per VigieActivity");

bool equals(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

void copy_text(char* dst, size_t dst_size, const char* src) {
    if (dst_size == 0) return;
    size_t i = 0;
    for (; src != nullptr && src[i] != '\0' && i + 1 < dst_size; ++i) dst[i] = src[i];
    dst[i] = '\0';
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

// Empty or not a number: unknown.
float read_float(const char* field) {
    if (field == nullptr || field[0] == '\0') return kVigieUnknown;
    char* end = nullptr;
    const double v = std::strtod(field, &end);
    if (end == field || *end != '\0' || !std::isfinite(v)) return kVigieUnknown;
    return static_cast<float>(v);
}

// Digits only; empty or anything else: 0 (unknown).
uint32_t read_u32(const char* field) {
    if (field == nullptr || field[0] == '\0') return 0;
    uint64_t v = 0;
    for (const char* p = field; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') return 0;
        v = v * 10 + static_cast<uint64_t>(*p - '0');
        if (v > 0xFFFFFFFFULL) return 0;
    }
    return static_cast<uint32_t>(v);
}

// Points: 0..100, else -1 (unknown).
int8_t read_points(const char* field) {
    const uint32_t v = read_u32(field);
    if (field == nullptr || field[0] == '\0' || v > 100) return -1;
    return static_cast<int8_t>(v);
}

void read_hours(const char* text, VigieVerdict out[kVigieSeaHours]) {
    const bool whole = text != nullptr && std::strlen(text) == static_cast<size_t>(kVigieSeaHours);
    for (int i = 0; i < kVigieSeaHours; ++i) {
        out[i] = VigieVerdict::UNKNOWN;
        if (!whole) continue;
        switch (text[i]) {
            case 'G': out[i] = VigieVerdict::GOOD; break;
            case 'P': out[i] = VigieVerdict::POSSIBLE; break;
            case 'X': out[i] = VigieVerdict::POOR; break;
            default: break;
        }
    }
}

void read_daylight(const char* text, VigieDaylight out[kVigieSeaHours]) {
    const bool whole = text != nullptr && std::strlen(text) == static_cast<size_t>(kVigieSeaHours);
    for (int i = 0; i < kVigieSeaHours; ++i) {
        out[i] = VigieDaylight::UNKNOWN;
        if (!whole) continue;
        if (text[i] == '1') out[i] = VigieDaylight::DAY;
        if (text[i] == '0') out[i] = VigieDaylight::NIGHT;
    }
}

// "H:1794149460:1.71,L:1794170000:-1.66" → tide extremes (at most kVigieMaxTideExtremes).
void read_tide(char* text, VigieSea& sea) {
    sea.tide_count = 0;
    char* p = text;
    while (*p != '\0' && sea.tide_count < kVigieMaxTideExtremes) {
        char* item = p;
        char* comma = std::strchr(p, ',');
        if (comma != nullptr) {
            *comma = '\0';
            p = comma + 1;
        } else {
            p = item + std::strlen(item);
        }
        char* parts[3] = {};
        if (split_fields(item, ':', parts, 3) != 3) continue;
        if ((parts[0][0] != 'H' && parts[0][0] != 'L') || parts[0][1] != '\0') continue;
        const uint32_t utc = read_u32(parts[1]);
        if (utc == 0) continue;
        VigieTideExtreme& e = sea.tide[sea.tide_count++];
        e.high = parts[0][0] == 'H';
        e.utc = utc;
        e.height_m = read_float(parts[2]);
    }
}

// "1794162580:97,1794207100:93" → estimated coefficients (at most kVigieMaxTideCoefficients).
void read_coefficients(char* text, VigieSea& sea) {
    sea.coefficient_count = 0;
    char* p = text;
    while (*p != '\0' && sea.coefficient_count < kVigieMaxTideCoefficients) {
        char* item = p;
        char* comma = std::strchr(p, ',');
        if (comma != nullptr) {
            *comma = '\0';
            p = comma + 1;
        } else {
            p = item + std::strlen(item);
        }
        char* parts[2] = {};
        if (split_fields(item, ':', parts, 2) != 2) continue;
        const uint32_t utc = read_u32(parts[0]);
        const uint32_t value = read_u32(parts[1]);
        if (utc == 0 || value == 0 || value > 200) continue;
        VigieTideCoefficient& c = sea.coefficients[sea.coefficient_count++];
        c.high_water_utc = utc;
        c.value = static_cast<uint16_t>(value);
    }
}

// "swell_height.too_small", "vigilance.orange" → criterion and state; unknown names stay UNKNOWN.
void read_reason(char* text, VigieCriterionId& criterion, VigieCriterionState& state) {
    criterion = VigieCriterionId::UNKNOWN;
    state = VigieCriterionState::UNKNOWN;
    char* dot = std::strchr(text, '.');
    if (dot == nullptr) return;
    *dot = '\0';
    const VigieCriterionId c = vigie_criterion_from_name(text);
    const VigieCriterionState s = vigie_criterion_state_from_name(dot + 1);
    if (c == VigieCriterionId::UNKNOWN || s == VigieCriterionState::UNKNOWN) return;
    criterion = c;
    state = s;
}

// SEA|... : false when the line cannot be used.
bool read_sea_line(char* f[], int n, VigieSea& sea) {
    if (n < kSeaFields || f[1][0] == '\0') return false;
    const uint32_t generated = read_u32(f[3]);
    if (generated == 0) return false;
    sea.valid = true;
    copy_text(sea.name, sizeof(sea.name), f[1]);
    copy_text(sea.place, sizeof(sea.place), f[2]);
    sea.generated_utc = generated;
    sea.first_hour_utc = read_u32(f[4]);
    sea.stale = f[5][0] == '1';
    sea.wind_min_kn = read_float(f[6]);
    sea.wind_max_kn = read_float(f[7]);
    sea.sunrise_utc = read_u32(f[8]);
    sea.sunset_utc = read_u32(f[9]);
    sea.sunrise2_utc = read_u32(f[10]);
    sea.sunset2_utc = read_u32(f[11]);
    read_tide(f[12], sea);
    read_coefficients(f[13], sea);
    // The warnings: "ok" means they were read, and an empty level then means none stands. Any other
    // status, or a level this firmware does not know, is "could not tell" (never "nothing to fear").
    sea.warnings_known = equals(f[14], "ok");
    sea.warning_present = false;
    if (sea.warnings_known && f[15][0] != '\0') {
        if (equals(f[15], "GREEN")) sea.warning_level = VigieSeverity::GREEN;
        else if (equals(f[15], "YELLOW")) sea.warning_level = VigieSeverity::YELLOW;
        else if (equals(f[15], "ORANGE")) sea.warning_level = VigieSeverity::ORANGE;
        else if (equals(f[15], "RED")) sea.warning_level = VigieSeverity::RED;
        else sea.warnings_known = false;
        sea.warning_present = sea.warnings_known;
    }
    if (sea.warning_present) {
        sea.warning_until_utc = read_u32(f[16]);
        copy_text(sea.warning_source, sizeof(sea.warning_source), f[17]);
    }
    return true;
}

// ACT|... : the activity index, or -1 when the line cannot be used.
int read_act_line(char* f[], int n, VigieSea& sea) {
    if (n < kActFields) return -1;
    const VigieActivity activity = vigie_activity_from_name(f[1]);
    if (activity == VigieActivity::COUNT_) return -1;
    const int index = static_cast<int>(activity);
    VigieActivityForecast& a = sea.activities[index];
    a = VigieActivityForecast{};
    a.present = true;
    a.available = f[2][0] == '1';
    if (!a.available) return index;  // fields 3 to 11 are empty then
    a.verdict = vigie_verdict_from_name(f[3]);
    if (a.verdict != VigieVerdict::UNKNOWN) read_reason(f[4], a.reason_criterion, a.reason_state);
    a.slot_start_utc = read_u32(f[5]);
    a.slot_end_utc = read_u32(f[6]);
    if (a.slot_start_utc == 0 || a.slot_end_utc <= a.slot_start_utc) a.slot_start_utc = a.slot_end_utc = 0;
    read_hours(f[7], a.hours);
    read_daylight(f[8], a.daylight);
    a.detail_utc = read_u32(f[9]);
    a.points = read_points(f[10]);
    a.out_of = read_points(f[11]);
    return index;
}

// CRIT|... of the ACT line `current`.
void read_crit_line(char* f[], int n, VigieSea& sea, int current) {
    if (n < kCritFields || current < 0) return;
    if (vigie_activity_from_name(f[1]) != static_cast<VigieActivity>(current)) return;
    VigieActivityForecast& a = sea.activities[current];
    if (a.criterion_count >= kVigieMaxCriteria) return;
    VigieCriterion& c = a.criteria[a.criterion_count];
    c = VigieCriterion{};
    c.id = vigie_criterion_from_name(f[2]);
    if (c.id == VigieCriterionId::UNKNOWN) return;  // not counted: a criterion this firmware cannot name
    ++a.criterion_count;
    c.value = read_float(f[3]);
    c.unit = vigie_unit_from_name(f[4]);
    c.state = vigie_criterion_state_from_name(f[5]);
    c.verdict = vigie_verdict_from_name(f[6]);
    c.points = read_points(f[7]);
    c.max_points = read_points(f[8]);
}

// True when a line of `frame` starts with `tag` followed by '|'.
bool has_tag_line(const char* frame, const char* tag) {
    const size_t len = std::strlen(tag);
    for (const char* line = frame; line != nullptr && *line != '\0';) {
        if (std::strncmp(line, tag, len) == 0 && line[len] == '|') return true;
        line = std::strchr(line, '\n');
        if (line != nullptr) ++line;
    }
    return false;
}

int verdict_rank(VigieVerdict v) {
    switch (v) {
        case VigieVerdict::GOOD: return 3;
        case VigieVerdict::POSSIBLE: return 2;
        case VigieVerdict::POOR: return 1;
        default: return 0;
    }
}

}  // namespace

// ─── Names ──────────────────────────────────────────────────────────────────

VigieActivity vigie_activity_from_name(const char* name) {
    if (name == nullptr) return VigieActivity::COUNT_;
    for (size_t i = 0; i < static_cast<size_t>(VigieActivity::COUNT_); ++i) {
        if (equals(name, kActivityNames[i])) return static_cast<VigieActivity>(i);
    }
    return VigieActivity::COUNT_;
}

VigieVerdict vigie_verdict_from_name(const char* name) {
    if (name == nullptr) return VigieVerdict::UNKNOWN;
    if (equals(name, "good")) return VigieVerdict::GOOD;
    if (equals(name, "possible")) return VigieVerdict::POSSIBLE;
    if (equals(name, "poor")) return VigieVerdict::POOR;
    return VigieVerdict::UNKNOWN;
}

VigieCriterionId vigie_criterion_from_name(const char* name) {
    if (name == nullptr || name[0] == '\0') return VigieCriterionId::UNKNOWN;
    for (size_t i = 1; i < static_cast<size_t>(VigieCriterionId::COUNT_); ++i) {
        if (equals(name, kCriterionNames[i])) return static_cast<VigieCriterionId>(i);
    }
    return VigieCriterionId::UNKNOWN;
}

VigieCriterionState vigie_criterion_state_from_name(const char* name) {
    if (name == nullptr || name[0] == '\0') return VigieCriterionState::UNKNOWN;
    for (size_t i = 1; i < static_cast<size_t>(VigieCriterionState::COUNT_); ++i) {
        if (equals(name, kStateNames[i])) return static_cast<VigieCriterionState>(i);
    }
    return VigieCriterionState::UNKNOWN;
}

VigieUnit vigie_unit_from_name(const char* name) {
    if (name == nullptr || name[0] == '\0') return VigieUnit::NONE;
    if (equals(name, "m")) return VigieUnit::METRES;
    if (equals(name, "s")) return VigieUnit::SECONDS;
    if (equals(name, "deg")) return VigieUnit::DEGREES;
    if (equals(name, "km/h")) return VigieUnit::KMH;
    if (equals(name, "kn")) return VigieUnit::KNOTS;
    if (equals(name, "Beaufort")) return VigieUnit::BEAUFORT;
    if (equals(name, "min")) return VigieUnit::MINUTES;
    return VigieUnit::UNKNOWN;
}

// ─── Decoding ───────────────────────────────────────────────────────────────

bool vigie_decode_sea(const char* frame, VigieSea& sea) {
    if (frame == nullptr || frame[0] == '\0' || !vigie_frame_crc_ok(frame)) return false;

    VigieSea result{};
    bool have_sea = false;
    int current = -1;  // the activity of the last ACT line, for the CRIT lines after it
    const char* cursor = frame;
    while (*cursor != '\0') {
        while (*cursor == '\r' || *cursor == '\n' || *cursor == ' ' || *cursor == '\t') ++cursor;
        if (*cursor == '\0') break;
        const char* end = cursor;
        while (*end != '\0' && *end != '\r' && *end != '\n') ++end;
        const size_t len = static_cast<size_t>(end - cursor);
        const char* start = cursor;
        cursor = end;

        char line[kLineMax];
        const size_t copy = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
        std::memcpy(line, start, copy);
        line[copy] = '\0';
        char* f[kFieldsMax] = {};
        const int n = split_fields(line, '|', f, kFieldsMax);
        if (n < 2) continue;

        if (equals(f[0], "END")) break;
        if (equals(f[0], "SEA")) {
            if (have_sea) continue;  // one SEA line per frame: the first wins
            have_sea = read_sea_line(f, n, result);
            if (!have_sea) result = VigieSea{};
            continue;
        }
        if (!have_sea) continue;  // ACT and CRIT only make sense after their SEA line
        if (equals(f[0], "ACT")) {
            current = read_act_line(f, n, result);
            continue;
        }
        if (equals(f[0], "CRIT")) read_crit_line(f, n, result, current);
    }
    if (!have_sea) return false;
    sea = result;
    return true;
}

VigieSeaUpdate vigie_update_sea(const char* frame, VigieSea& sea) {
    if (frame == nullptr || frame[0] == '\0' || !vigie_frame_crc_ok(frame)) return VigieSeaUpdate::REFUSED;
    if (!has_tag_line(frame, "SEA")) return VigieSeaUpdate::NO_BLOCK;
    return vigie_decode_sea(frame, sea) ? VigieSeaUpdate::UPDATED : VigieSeaUpdate::REFUSED;
}

// ─── Reading ────────────────────────────────────────────────────────────────

int vigie_sea_hour_index(const VigieSea& sea, uint32_t now_utc) {
    if (!sea.valid || sea.first_hour_utc == 0 || now_utc < sea.first_hour_utc) return -1;
    const uint32_t index = (now_utc - sea.first_hour_utc) / 3600u;
    return index < static_cast<uint32_t>(kVigieSeaHours) ? static_cast<int>(index) : -1;
}

VigieSeaFreshness vigie_sea_freshness(const VigieSea& sea, uint32_t now_utc) {
    if (!sea.valid) return VigieSeaFreshness::NONE;
    if (now_utc == 0) return VigieSeaFreshness::STALE;  // no clock: the age is not known
    const uint32_t age = now_utc > sea.generated_utc ? now_utc - sea.generated_utc : 0;
    if (age > kVigieSeaExpiredAfterS) return VigieSeaFreshness::EXPIRED;
    // The 24 hours of the strips are over: the verdicts say nothing about now.
    if (sea.first_hour_utc != 0 && now_utc >= sea.first_hour_utc + static_cast<uint32_t>(kVigieSeaHours) * 3600u) {
        return VigieSeaFreshness::EXPIRED;
    }
    if (sea.stale || age > kVigieSeaStaleAfterS) return VigieSeaFreshness::STALE;
    return VigieSeaFreshness::FRESH;
}

int vigie_sea_best_activity(const VigieSea& sea) {
    int best = -1;
    int best_rank = 0;
    for (int i = 0; i < static_cast<int>(VigieActivity::COUNT_); ++i) {
        const VigieActivityForecast& a = sea.activities[i];
        if (!a.present || !a.available) continue;
        const int rank = verdict_rank(a.verdict);
        if (rank > best_rank) {
            best = i;
            best_rank = rank;
        }
    }
    return best;
}

int vigie_sea_next_tide(const VigieSea& sea, uint32_t now_utc) {
    for (int i = 0; i < sea.tide_count && i < kVigieMaxTideExtremes; ++i) {
        if (sea.tide[i].utc >= now_utc) return i;
    }
    return -1;
}

bool vigie_spot_id_valid(const char* spot_id) {
    if (spot_id == nullptr) return false;
    const size_t len = std::strlen(spot_id);
    if (len == 0 || len > 64) return false;
    for (size_t i = 0; i < len; ++i) {
        const char c = spot_id[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!ok) return false;
    }
    return true;
}
