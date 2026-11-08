/**
 * @file vigie_sea.h
 * @brief The sea of a surf spot as the API frame gives it: verdicts per activity, tide, sun, warning.
 *
 * Decodes the `SEA`, `ACT` and `CRIT` lines vigie-api adds to the embedded frame when a surf spot is
 * asked for (`&spot=<id>`; vigie-api `docs/embedded-frame.md`, "Activities of a surf spot"). Holds
 * what a screen needs to answer "can I go today?": for surf, kite (kite, wing, windsurf), boat and
 * shore fishing, a verdict per hour for 24 hours, the best slot, the criteria behind the verdict;
 * the tide (high and low waters, estimated coefficients), the sun, the strongest coastal warning.
 *
 * - Separate from VigieWorldState on purpose: that struct is saved in NVS and the sea is not.
 * - An empty field is unknown and stays unknown (AGENTS.md rule 3). A frame without the block is
 *   unknown too, never "nothing to do": vigie_update_sea() then keeps the previous sea, and
 *   vigie_sea_freshness() says how old it is.
 * - Every verdict is a rule of Vigie, not an official one, and not for safety; the coefficient is
 *   an estimate, not the SHOM's. The texts that say so are in vigie_text.* .
 * - Times are UTC epoch seconds; the frame has no UTC offset of the spot (the screen uses the one
 *   of the zone it shows).
 *
 * Constraints: C++17, no allocation, no ESPHome nor LVGL; tested on a PC by tools/test_vigie_sea.cpp.
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "vigie_model.h"

// ─── Capacities ─────────────────────────────────────────────────────────────
constexpr int kVigieSeaHours = 24;          // hours of the strips of an activity
constexpr int kVigieMaxTideExtremes = 8;    // as the API caps them
constexpr int kVigieMaxTideCoefficients = 4;
constexpr int kVigieMaxCriteria = 6;        // surf has five
constexpr size_t kVigieSpotNameMax = 40;    // the API sends at most 32 characters
constexpr size_t kVigieSourceMax = 28;      // at most 24 characters

// ─── Freshness ──────────────────────────────────────────────────────────────
// The display asks for the frame every 60 s and the API works the verdicts out at each request, so
// the age of `generated_utc` is the age of the last frame that carried the block.
constexpr uint32_t kVigieSeaStaleAfterS = 600;      // older than this: say how old
constexpr uint32_t kVigieSeaExpiredAfterS = 21600;  // older than this: do not show the verdicts

// ─── Enumerations ───────────────────────────────────────────────────────────
// In the order of the frame.
enum class VigieActivity : uint8_t { SURF = 0, KITE = 1, BOAT = 2, SHORE_FISHING = 3, COUNT_ };

enum class VigieVerdict : uint8_t {
    UNKNOWN = 0,  // no verdict (empty, or '-' in a strip)
    GOOD = 1,
    POSSIBLE = 2,
    POOR = 3,     // the screen says "not advised"
};

enum class VigieDaylight : uint8_t { UNKNOWN = 0, NIGHT = 1, DAY = 2 };

enum class VigieUnit : uint8_t {
    NONE = 0,  // no unit (the coefficient)
    METRES,
    SECONDS,
    DEGREES,
    KMH,
    KNOTS,
    BEAUFORT,
    MINUTES,
    UNKNOWN = 255,  // a unit this firmware does not know: the value is not shown
};

// The criteria of the contract's vocabulary table, plus the coastal warning (a reason only).
enum class VigieCriterionId : uint8_t {
    UNKNOWN = 0,
    SWELL_HEIGHT,
    SWELL_PERIOD,
    SWELL_DIRECTION,
    WIND,
    TIDE,
    GUST_GAP,
    WIND_DIRECTION,
    WIND_FORCE,
    GUST_FORCE,
    SEA_HEIGHT,
    LOW_WATER,
    COEFFICIENT,
    VIGILANCE,
    COUNT_
};

// What a rule makes of a value. One list for every criterion: which states go with which criterion
// is the table in vigie_text.cpp (a pair that is not in it has no text and is not shown).
enum class VigieCriterionState : uint8_t {
    UNKNOWN = 0,
    TOO_SMALL, SMALL, RIGHT, BIG, TOO_BIG,
    SHORT, MEDIUM, LONG,
    FACING, OBLIQUE, OFF_AXIS,
    LIGHT, OFFSHORE, CROSS_SHORE, ONSHORE, STRONG,
    RIGHT_STAGE, WRONG_STAGE,
    IN_RANGE, A_BIT_LIGHT, A_BIT_STRONG, TOO_LIGHT, TOO_STRONG,
    STEADY, GUSTY, VERY_GUSTY,
    UP_TO_MODERATE, FRESH, STRONG_OR_MORE,
    UP_TO_FRESH, GALE_OR_MORE, NEAR_GALE_OR_MORE,
    LOW, MODERATE, HIGH,
    IN_WINDOW, OUTSIDE_WINDOW,
    LARGE, VERY_LARGE,
    ORANGE, RED,  // the coastal warning
    COUNT_
};

// ─── State ──────────────────────────────────────────────────────────────────
struct VigieCriterion {
    VigieCriterionId id = VigieCriterionId::UNKNOWN;
    VigieCriterionState state = VigieCriterionState::UNKNOWN;  // unknown: value or reference unknown
    float value = kVigieUnknown;
    VigieUnit unit = VigieUnit::NONE;
    VigieVerdict verdict = VigieVerdict::UNKNOWN;  // unknown: the criterion counts for nothing
    int8_t points = -1;      // surf only; -1 unknown
    int8_t max_points = -1;  // surf only; -1 unknown
};

struct VigieActivityForecast {
    bool present = false;    // an ACT line was received for this activity
    bool available = false;  // false: cannot be judged here (shore fishing without a tide model)
    VigieVerdict verdict = VigieVerdict::UNKNOWN;  // best of the daylight hours of the 24 hours
    // Why it is not good: a criterion and its state, or the coastal warning (orange / red).
    VigieCriterionId reason_criterion = VigieCriterionId::UNKNOWN;
    VigieCriterionState reason_state = VigieCriterionState::UNKNOWN;
    uint32_t slot_start_utc = 0;  // best slot; 0 = none (poor, or unknown)
    uint32_t slot_end_utc = 0;    // exclusive
    VigieVerdict hours[kVigieSeaHours] = {};      // from VigieSea::first_hour_utc, one per hour
    VigieDaylight daylight[kVigieSeaHours] = {};
    uint32_t detail_utc = 0;  // the hour the verdict stands on: the criteria below are its
    int8_t points = -1;       // surf only: points of that hour; -1 unknown
    int8_t out_of = -1;       // surf only: the most the criteria that could be scored give
    uint8_t criterion_count = 0;
    VigieCriterion criteria[kVigieMaxCriteria] = {};
};

struct VigieTideExtreme {
    bool high = false;
    uint32_t utc = 0;
    float height_m = kVigieUnknown;  // to the model's mean sea level
};

struct VigieTideCoefficient {
    uint32_t high_water_utc = 0;  // the Brest high water it belongs to
    uint16_t value = 0;           // estimated by Vigie, not the SHOM's
};

struct VigieSea {
    bool valid = false;  // a SEA line was decoded
    char name[kVigieSpotNameMax] = "";
    char place[kVigieSpotNameMax] = "";  // nearest town or village; may be empty
    uint32_t generated_utc = 0;
    uint32_t first_hour_utc = 0;  // start of hour 0 of the strips; 0 = no activity has an hour
    bool stale = false;           // a provider did not answer: its last answer is served
    float wind_min_kn = kVigieUnknown;  // the kite's range
    float wind_max_kn = kVigieUnknown;
    uint32_t sunrise_utc = 0;  // 0 unknown; the first sunset still to come and the sunrise of that day
    uint32_t sunset_utc = 0;
    uint32_t sunrise2_utc = 0;  // the day after
    uint32_t sunset2_utc = 0;
    uint8_t tide_count = 0;  // 0 where no tide model is near the spot
    VigieTideExtreme tide[kVigieMaxTideExtremes] = {};
    uint8_t coefficient_count = 0;
    VigieTideCoefficient coefficients[kVigieMaxTideCoefficients] = {};
    // Official coastal warnings (in France the wave-submersion vigilance). false: they could not be
    // read, which is NOT "nothing to fear".
    bool warnings_known = false;
    bool warning_present = false;  // a warning stands at some moment of the 24 hours
    VigieSeverity warning_level = VigieSeverity::GREEN;
    uint32_t warning_until_utc = 0;  // 0: unknown end
    char warning_source[kVigieSourceMax] = "";
    VigieActivityForecast activities[static_cast<size_t>(VigieActivity::COUNT_)] = {};
};

enum class VigieSeaFreshness : uint8_t {
    NONE = 0,     // nothing received
    FRESH = 1,
    STALE = 2,    // the API says it serves a last answer, or the data is some minutes old: show its age
    EXPIRED = 3,  // too old, or the 24 hours are over: do not show the verdicts
};

enum class VigieSeaUpdate : uint8_t {
    UPDATED = 0,   // the sea was replaced
    NO_BLOCK = 1,  // a good frame without the block (no spot asked, or the swell could not be read)
    REFUSED = 2,   // not a valid frame (CRC) or a SEA line that cannot be read
};

// ─── Decoding ───────────────────────────────────────────────────────────────
// Decodes SEA / ACT / CRIT lines of `frame` into `sea` (replaced). The CRC is checked as for the
// rest of the frame. False, `sea` untouched, when the CRC is wrong or there is no usable SEA line.
// Tags it does not know are skipped. A CRIT line belongs to the ACT line just above it.
bool vigie_decode_sea(const char* frame, VigieSea& sea);
// What the firmware calls with each good frame: replaces `sea` only when the frame carries the block.
VigieSeaUpdate vigie_update_sea(const char* frame, VigieSea& sea);

// ─── Reading ────────────────────────────────────────────────────────────────
// Names of the contract ("surf", "good", "swell_height", "too_small"); UNKNOWN when not known.
VigieActivity vigie_activity_from_name(const char* name);  // COUNT_ when not known
VigieVerdict vigie_verdict_from_name(const char* name);
VigieCriterionId vigie_criterion_from_name(const char* name);
VigieCriterionState vigie_criterion_state_from_name(const char* name);
VigieUnit vigie_unit_from_name(const char* name);

// Index (0..23) of the hour of `now_utc` in the strips, -1 when it is outside them.
int vigie_sea_hour_index(const VigieSea& sea, uint32_t now_utc);
VigieSeaFreshness vigie_sea_freshness(const VigieSea& sea, uint32_t now_utc);
// The activity with the best verdict (ties: the first of the frame's order), -1 when none has one.
int vigie_sea_best_activity(const VigieSea& sea);
// Index in `sea.tide` of the first high or low water at or after `now_utc`, -1 when none.
int vigie_sea_next_tide(const VigieSea& sea, uint32_t now_utc);
// Is `spot_id` usable in the request (a slug: a-z, 0-9, '-', '_'; at most 64 characters)?
bool vigie_spot_id_valid(const char* spot_id);

// Height of the water at `utc` (metres): a half cosine between the high and low waters on either side
// of it, which is how a tide is drawn when only its extremes are known. False, `metres` untouched,
// outside the first and the last extreme or when one of the two heights is unknown: nothing is
// guessed beyond what the frame says. The extremes must be in time order, as the API sends them.
bool vigie_tide_height_at(const VigieSea& sea, uint32_t utc, float& metres);
// Index in `sea.coefficients` of the first coefficient whose high water is at or after `now_utc`
// (the one of the next high water), -1 when none.
int vigie_sea_coefficient_for(const VigieSea& sea, uint32_t now_utc);

// The sea the screen shows, replaced by vigie_update_sea() with each good frame.
extern VigieSea g_vigie_sea;
