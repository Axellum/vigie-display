// The vocabulary of the CRIT lines, copied from the contract (vigie-api docs/embedded-frame.md,
// table "Activity / Criterion / Unit / States"). Shared by the PC tests: tools/test_vigie_sea.cpp
// checks that the firmware reads every name, tools/test_vigie_text.cpp that every pair has a text in
// both languages. When the contract adds a criterion or a state, add it here first: the tests fail
// until vigie_sea.* and vigie_text.cpp know it.
#pragma once

#include "vigie_sea.h"

struct VocabularyRow {
    VigieActivity activity;
    const char* criterion;
    const char* unit;
    const char* states[6];  // nullptr after the last
};

constexpr VocabularyRow kContractVocabulary[] = {
    {VigieActivity::SURF, "swell_height", "m", {"too_small", "small", "right", "big", "too_big", nullptr}},
    {VigieActivity::SURF, "swell_period", "s", {"short", "medium", "long", nullptr}},
    {VigieActivity::SURF, "swell_direction", "deg", {"facing", "oblique", "off_axis", nullptr}},
    {VigieActivity::SURF, "wind", "km/h", {"light", "offshore", "cross_shore", "onshore", "strong", nullptr}},
    {VigieActivity::SURF, "tide", "m", {"right_stage", "wrong_stage", nullptr}},
    {VigieActivity::KITE, "wind", "kn", {"in_range", "a_bit_light", "a_bit_strong", "too_light", "too_strong", nullptr}},
    {VigieActivity::KITE, "gust_gap", "kn", {"steady", "gusty", "very_gusty", nullptr}},
    {VigieActivity::KITE, "wind_direction", "deg", {"onshore", "offshore", "cross_shore", nullptr}},
    {VigieActivity::BOAT, "wind_force", "Beaufort", {"up_to_moderate", "fresh", "strong_or_more", nullptr}},
    {VigieActivity::BOAT, "gust_force", "Beaufort", {"up_to_fresh", "strong", "gale_or_more", nullptr}},
    {VigieActivity::BOAT, "sea_height", "m", {"low", "moderate", "high", nullptr}},
    {VigieActivity::SHORE_FISHING, "low_water", "min", {"in_window", "outside_window", nullptr}},
    {VigieActivity::SHORE_FISHING, "coefficient", "", {"small", "moderate", "large", "very_large", nullptr}},
    {VigieActivity::SHORE_FISHING, "wind_force", "Beaufort", {"up_to_fresh", "strong", "near_gale_or_more", nullptr}},
    {VigieActivity::SHORE_FISHING, "sea_height", "m", {"low", "moderate", "high", nullptr}},
};
