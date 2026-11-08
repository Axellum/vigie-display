/**
 * @file vigie_text.cpp
 * @brief Implementation of vigie_text.h (see the header).
 */
#include "vigie_text.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

VigieLanguage g_language = VigieLanguage::EN;

bool fr() { return g_language == VigieLanguage::FR; }

// Picks the text of the current language.
const char* pick(const char* en, const char* french) { return fr() ? french : en; }

bool written(int n, size_t size) { return n >= 0 && static_cast<size_t>(n) < size; }

bool clear(char* buf, size_t size) {
    if (buf == nullptr || size == 0) return false;
    buf[0] = '\0';
    return true;
}

struct Pair {
    const char* en;
    const char* fr;
};

// One line per VigieText, in the enum's order (checked by a static_assert below).
constexpr Pair kTexts[] = {
    {"Active alerts", "Alertes en cours"},
    {"No active alert", "Aucune alerte en cours"},
    {"Alert sources unreachable", "Sources d'alertes injoignables"},
    {"Waiting for data…", "En attente des données…"},
    {"No weather data", "Pas de données météo"},
    {"Wind", "Vent"},
    {"Gusts", "Rafales"},
    {"Humidity", "Humidité"},
    {"Rain, next hour", "Pluie dans l'heure"},
    {"Air quality", "Qualité de l'air"},
    {"Waves", "Vagues"},
    {"feels like %s", "ressenti %s"},
    {"none", "aucune"},
    {"now", "en cours"},
    {"in %d min", "dans %d min"},
    {"Waiting for the API", "En attente de l'API"},
    {"Up to date · %s ago", "À jour · il y a %s"},
    {"API unreachable", "API injoignable"},
    {"API unreachable · data from %s ago", "API injoignable · données d'il y a %s"},
    {"Sea data unavailable", "Mer : données indisponibles"},
    {"Sea data · %s", "Mer · %s"},
    {"Some sea data is from an earlier reading", "Certaines données de mer datent d'une lecture précédente"},
    {"Sea data too old", "Données de mer trop anciennes"},
    {"Vigie's own rules, not for safety", "Règles de Vigie, pas pour la sécurité"},
    {"Coastal warnings unavailable", "Vigilances côtières indisponibles"},
    {"%s coastal warning", "Vigilance côtière %s"},
    {"%s coastal warning · until %s", "Vigilance côtière %s · jusqu'à %s"},
    {"Not available here", "Indisponible ici"},
    {"High water", "Pleine mer"},
    {"Low water", "Basse mer"},
    {"Coefficient %u (estimated)", "Coefficient %u (estimé)"},
};
static_assert(sizeof(kTexts) / sizeof(kTexts[0]) == static_cast<size_t>(VigieText::COUNT_),
              "kTexts must have one line per VigieText");

constexpr const char* kDaysEn[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
constexpr const char* kDaysFr[7] = {"dimanche", "lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi"};
constexpr const char* kDaysShortEn[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
constexpr const char* kDaysShortFr[7] = {"dim.", "lun.", "mar.", "mer.", "jeu.", "ven.", "sam."};
constexpr const char* kMonthsEn[12] = {"January", "February", "March",     "April",   "May",      "June",
                                       "July",    "August",   "September", "October", "November", "December"};
constexpr const char* kMonthsFr[12] = {"janvier", "février", "mars",      "avril",   "mai",      "juin",
                                       "juillet", "août",    "septembre", "octobre", "novembre", "décembre"};

int64_t floor_div(int64_t a, int64_t b) {
    const int64_t q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

int64_t local_seconds(uint32_t utc, int32_t offset_s) {
    return static_cast<int64_t>(utc) + static_cast<int64_t>(offset_s);
}

// Days since 1970-01-01 → civil date. Howard Hinnant's "civil_from_days" (public domain).
void civil_from_days(int64_t days, int& year, int& month, int& day) {
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const int64_t doe = days - era * 146097;
    const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const int64_t mp = (5 * doy + 2) / 153;
    day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    year = static_cast<int>(yoe + era * 400 + (month <= 2 ? 1 : 0));
}

// Civil date → days since 1970-01-01 (same author, "days_from_civil").
int64_t days_from_civil(int year, int month, int day) {
    year -= month <= 2 ? 1 : 0;
    const int64_t era = (year >= 0 ? year : year - 399) / 400;
    const int64_t yoe = year - era * 400;
    const int64_t doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

int weekday(int64_t days) { return static_cast<int>(((days % 7) + 7 + 4) % 7); }  // 1970-01-01: Thursday

}  // namespace

// ─── Language ───────────────────────────────────────────────────────────────

void vigie_set_language(VigieLanguage language) { g_language = language; }
VigieLanguage vigie_language() { return g_language; }

VigieLanguage vigie_language_from_code(const char* code) {
    return (code != nullptr && (code[0] == 'f' || code[0] == 'F') && (code[1] == 'r' || code[1] == 'R'))
               ? VigieLanguage::FR
               : VigieLanguage::EN;
}

const char* vigie_tr(VigieText id) {
    const size_t i = static_cast<size_t>(id);
    if (i >= sizeof(kTexts) / sizeof(kTexts[0])) return "";
    return pick(kTexts[i].en, kTexts[i].fr);
}

// ─── Sea texts: tables ──────────────────────────────────────────────────────

namespace {

constexpr Pair kActivityNames[] = {
    {"Surf", "Surf"},
    {"Kite / wing", "Kite / wing"},
    {"Boat", "Bateau"},
    {"Shore fishing", "Pêche à pied"},
};
static_assert(sizeof(kActivityNames) / sizeof(kActivityNames[0]) == static_cast<size_t>(VigieActivity::COUNT_),
              "kActivityNames must have one line per VigieActivity");

// Indexed by VigieVerdict.
constexpr Pair kVerdictLabels[] = {{"", ""}, {"Good", "Bon"}, {"Possible", "Possible"}, {"Not advised", "Déconseillé"}};

// Indexed by VigieCriterionId.
constexpr Pair kCriterionLabels[] = {
    {"", ""},
    {"Swell", "Houle"},
    {"Swell period", "Période de houle"},
    {"Swell direction", "Direction de la houle"},
    {"Wind", "Vent"},
    {"Tide", "Marée"},
    {"Gust gap", "Écart des rafales"},
    {"Wind direction", "Direction du vent"},
    {"Wind", "Vent"},
    {"Gusts", "Rafales"},
    {"Sea", "Mer"},
    {"Low water", "Basse mer"},
    {"Coefficient (est.)", "Coefficient (est.)"},
    {"Coastal warning", "Vigilance côtière"},
};
static_assert(sizeof(kCriterionLabels) / sizeof(kCriterionLabels[0]) == static_cast<size_t>(VigieCriterionId::COUNT_),
              "kCriterionLabels must have one line per VigieCriterionId");

struct StateText {
    VigieCriterionId criterion;
    VigieCriterionState state;
    const char* en;
    const char* fr;
};

// The vocabulary of the contract (vigie-api docs/embedded-frame.md, table of the criteria), one
// line per pair; tools/test_vigie_sea.cpp keeps a copy of the contract's table and checks it.
using C = VigieCriterionId;
using S = VigieCriterionState;
constexpr StateText kStateTexts[] = {
    {C::SWELL_HEIGHT, S::TOO_SMALL, "too small", "trop petite"},
    {C::SWELL_HEIGHT, S::SMALL, "small", "petite"},
    {C::SWELL_HEIGHT, S::RIGHT, "right size", "bonne taille"},
    {C::SWELL_HEIGHT, S::BIG, "big", "grosse"},
    {C::SWELL_HEIGHT, S::TOO_BIG, "too big", "trop grosse"},
    {C::SWELL_PERIOD, S::SHORT, "short", "courte"},
    {C::SWELL_PERIOD, S::MEDIUM, "medium", "moyenne"},
    {C::SWELL_PERIOD, S::LONG, "long", "longue"},
    {C::SWELL_DIRECTION, S::FACING, "facing the beach", "face à la plage"},
    {C::SWELL_DIRECTION, S::OBLIQUE, "oblique", "de biais"},
    {C::SWELL_DIRECTION, S::OFF_AXIS, "off axis", "hors axe"},
    {C::WIND, S::LIGHT, "light", "faible"},
    {C::WIND, S::OFFSHORE, "offshore", "de terre"},
    {C::WIND, S::CROSS_SHORE, "cross-shore", "de travers"},
    {C::WIND, S::ONSHORE, "onshore", "de mer"},
    {C::WIND, S::STRONG, "strong", "fort"},
    {C::WIND, S::IN_RANGE, "in your range", "dans votre plage"},
    {C::WIND, S::A_BIT_LIGHT, "a bit light", "un peu faible"},
    {C::WIND, S::A_BIT_STRONG, "a bit strong", "un peu fort"},
    {C::WIND, S::TOO_LIGHT, "too light", "trop faible"},
    {C::WIND, S::TOO_STRONG, "too strong", "trop fort"},
    {C::TIDE, S::RIGHT_STAGE, "right stage", "bon moment"},
    {C::TIDE, S::WRONG_STAGE, "wrong stage", "mauvais moment"},
    {C::GUST_GAP, S::STEADY, "steady", "régulier"},
    {C::GUST_GAP, S::GUSTY, "gusty", "rafaleux"},
    {C::GUST_GAP, S::VERY_GUSTY, "very gusty", "très rafaleux"},
    {C::WIND_DIRECTION, S::ONSHORE, "onshore", "de mer"},
    {C::WIND_DIRECTION, S::OFFSHORE, "offshore", "de terre"},
    {C::WIND_DIRECTION, S::CROSS_SHORE, "cross-shore", "de travers"},
    {C::WIND_FORCE, S::UP_TO_MODERATE, "moderate or less", "modéré ou moins"},
    {C::WIND_FORCE, S::FRESH, "fresh", "bon frais"},
    {C::WIND_FORCE, S::STRONG_OR_MORE, "strong or more", "fort ou plus"},
    {C::WIND_FORCE, S::UP_TO_FRESH, "fresh or less", "bon frais ou moins"},
    {C::WIND_FORCE, S::STRONG, "strong", "fort"},
    {C::WIND_FORCE, S::NEAR_GALE_OR_MORE, "near gale or more", "grand frais ou plus"},
    {C::GUST_FORCE, S::UP_TO_FRESH, "fresh or less", "bon frais ou moins"},
    {C::GUST_FORCE, S::STRONG, "strong", "fort"},
    {C::GUST_FORCE, S::GALE_OR_MORE, "gale or more", "coup de vent ou plus"},
    {C::SEA_HEIGHT, S::LOW, "low", "faible"},
    {C::SEA_HEIGHT, S::MODERATE, "moderate", "modérée"},
    {C::SEA_HEIGHT, S::HIGH, "high", "forte"},
    {C::LOW_WATER, S::IN_WINDOW, "in the window", "dans la fenêtre"},
    {C::LOW_WATER, S::OUTSIDE_WINDOW, "outside the window", "hors fenêtre"},
    {C::COEFFICIENT, S::SMALL, "small", "faible"},
    {C::COEFFICIENT, S::MODERATE, "moderate", "moyen"},
    {C::COEFFICIENT, S::LARGE, "large", "fort"},
    {C::COEFFICIENT, S::VERY_LARGE, "very large", "très fort"},
    {C::VIGILANCE, S::ORANGE, "orange", "orange"},
    {C::VIGILANCE, S::RED, "red", "rouge"},
};

// Indexed by VigieSeverity.
constexpr Pair kWarningLevels[] = {{"Green", "verte"}, {"Yellow", "jaune"}, {"Orange", "orange"}, {"Red", "rouge"}};

}  // namespace

// ─── World map ──────────────────────────────────────────────────────────────

VigiePixel vigie_map_project(double lat_deg, double lon_deg, int32_t width, int32_t height) {
    VigiePixel p{};
    if (!std::isfinite(lat_deg) || !std::isfinite(lon_deg) || width <= 0 || height <= 0) return p;
    p.inside = lat_deg >= kVigieMapLatSouth && lat_deg <= kVigieMapLatNorth && lon_deg >= -180.0 && lon_deg <= 180.0;
    const double lat = lat_deg < kVigieMapLatSouth ? kVigieMapLatSouth : (lat_deg > kVigieMapLatNorth ? kVigieMapLatNorth : lat_deg);
    const double lon = lon_deg < -180.0 ? -180.0 : (lon_deg > 180.0 ? 180.0 : lon_deg);
    int32_t x = static_cast<int32_t>(std::floor((lon + 180.0) / 360.0 * width));
    int32_t y = static_cast<int32_t>(std::floor((kVigieMapLatNorth - lat) / (kVigieMapLatNorth - kVigieMapLatSouth) * height));
    p.x = x < 0 ? 0 : (x > width - 1 ? width - 1 : x);
    p.y = y < 0 ? 0 : (y > height - 1 ? height - 1 : y);
    return p;
}

// ─── Labels and icons ───────────────────────────────────────────────────────

const char* vigie_weather_label(uint16_t code) {
    if (code == kVigieUnknownCode) return "";
    switch (code) {
        case 0: return pick("Clear sky", "Ciel clair");
        case 1: return pick("Mainly clear", "Peu nuageux");
        case 2: return pick("Partly cloudy", "Éclaircies");
        case 3: return pick("Overcast", "Couvert");
        case 45:
        case 48: return pick("Fog", "Brouillard");
        case 51:
        case 53:
        case 55: return pick("Drizzle", "Bruine");
        case 56:
        case 57: return pick("Freezing drizzle", "Bruine verglaçante");
        case 61: return pick("Light rain", "Pluie faible");
        case 63: return pick("Rain", "Pluie");
        case 65: return pick("Heavy rain", "Forte pluie");
        case 66:
        case 67: return pick("Freezing rain", "Pluie verglaçante");
        case 71: return pick("Light snow", "Neige faible");
        case 73: return pick("Snow", "Neige");
        case 75: return pick("Heavy snow", "Forte neige");
        case 77: return pick("Snow grains", "Grésil");
        case 80:
        case 81: return pick("Showers", "Averses");
        case 82: return pick("Heavy showers", "Fortes averses");
        case 85:
        case 86: return pick("Snow showers", "Averses de neige");
        case 95: return pick("Thunderstorm", "Orage");
        case 96:
        case 99: return pick("Thunderstorm, hail", "Orage et grêle");
        default: return pick("Unknown", "Temps inconnu");
    }
}

const char* vigie_weather_icon(uint16_t code, bool night) {
    if (code == kVigieUnknownCode) return "";
    switch (code) {
        case 0:
        case 1: return night ? "\U000F0594" : "\U000F0599";  // weather-night / weather-sunny
        case 2: return night ? "\U000F0F31" : "\U000F0595";  // weather-night-partly-cloudy / -partly-cloudy
        case 45:
        case 48: return "\U000F0591";  // weather-fog
        case 51:
        case 53:
        case 55:
        case 56:
        case 57:
        case 61:
        case 63:
        case 80:
        case 81: return "\U000F0597";  // weather-rainy
        case 65:
        case 82: return "\U000F0596";  // weather-pouring
        case 66:
        case 67: return "\U000F067F";  // weather-snowy-rainy
        case 71:
        case 73:
        case 77:
        case 85: return "\U000F0598";  // weather-snowy
        case 75:
        case 86: return "\U000F0F36";  // weather-snowy-heavy
        case 95: return "\U000F067E";  // weather-lightning-rainy
        case 96:
        case 99: return "\U000F0592";  // weather-hail
        default: return "\U000F0590";  // weather-cloudy
    }
}

const char* vigie_hazard_name(VigieHazardType type) {
    switch (type) {
        case VigieHazardType::EARTHQUAKE: return pick("Earthquake", "Séisme");
        case VigieHazardType::TSUNAMI: return pick("Tsunami", "Tsunami");
        case VigieHazardType::VOLCANO: return pick("Volcano", "Volcan");
        case VigieHazardType::CYCLONE: return pick("Cyclone", "Cyclone");
        case VigieHazardType::TORNADO: return pick("Tornado", "Tornade");
        case VigieHazardType::FLOOD: return pick("Flood", "Inondation");
        case VigieHazardType::EXTREME_RAIN: return pick("Extreme rain", "Pluie extrême");
        case VigieHazardType::WILDFIRE: return pick("Wildfire", "Feu de forêt");
        case VigieHazardType::SOLAR_STORM: return pick("Solar storm", "Tempête solaire");
    }
    return pick("Alert", "Alerte");
}

const char* vigie_hazard_icon(VigieHazardType type) {
    switch (type) {
        case VigieHazardType::EARTHQUAKE: return "\U000F0430";    // pulse
        case VigieHazardType::TSUNAMI: return "\U000F1A81";       // tsunami
        case VigieHazardType::VOLCANO: return "\U000F1A83";       // volcano
        case VigieHazardType::CYCLONE: return "\U000F0898";       // weather-hurricane
        case VigieHazardType::TORNADO: return "\U000F0F38";       // weather-tornado
        case VigieHazardType::FLOOD: return "\U000F0EFA";         // home-flood
        case VigieHazardType::EXTREME_RAIN: return "\U000F0596";  // weather-pouring
        case VigieHazardType::WILDFIRE: return "\U000F0238";      // fire
        case VigieHazardType::SOLAR_STORM: return "\U000F17FE";   // sun-wireless
    }
    return "\U000F0026";  // alert
}

const char* vigie_aqi_label(uint16_t aqi) {
    if (aqi == kVigieUnknownCode) return "";
    if (aqi <= 20) return pick("Good", "bon");
    if (aqi <= 40) return pick("Fair", "correct");
    if (aqi <= 60) return pick("Moderate", "moyen");
    if (aqi <= 80) return pick("Poor", "médiocre");
    if (aqi <= 100) return pick("Very poor", "mauvais");
    return pick("Extremely poor", "très mauvais");
}

// ─── Numbers, distances, times ──────────────────────────────────────────────

bool vigie_format_decimal(float value, int decimals, char* buf, size_t size) {
    if (!clear(buf, size) || !std::isfinite(value) || decimals < 0 || decimals > 3) return false;
    char tmp[32];
    if (!written(std::snprintf(tmp, sizeof(tmp), "%.*f", decimals, static_cast<double>(value)), sizeof(tmp))) return false;
    const char* start = tmp;
    if (tmp[0] == '-') {  // "-0" or "-0.0": zero without a sign
        bool zero = true;
        for (const char* p = tmp + 1; *p != '\0'; ++p) zero = zero && (*p == '0' || *p == '.');
        if (zero) start = tmp + 1;
    }
    const size_t n = std::strlen(start);
    if (n + 1 > size) return false;
    const char separator = fr() ? ',' : '.';
    for (size_t i = 0; i <= n; ++i) buf[i] = start[i] == '.' ? separator : start[i];
    return true;
}

bool vigie_format_distance(float km, char* buf, size_t size) {
    if (!clear(buf, size) || !std::isfinite(km) || km < 0.0f || km > 50000.0f) return false;
    long rounded = std::lround(static_cast<double>(km));
    if (rounded >= 1000) rounded = (rounded + 50) / 100 * 100;  // 16,873 → 16,900
    if (rounded >= 1000) {
        return written(std::snprintf(buf, size, "%ld%s%03ld km", rounded / 1000, fr() ? " " : ",", rounded % 1000), size);
    }
    return written(std::snprintf(buf, size, "%ld km", rounded), size);
}

bool vigie_format_age(uint32_t now_utc, uint32_t event_utc, char* buf, size_t size) {
    if (!clear(buf, size) || now_utc == 0 || event_utc == 0) return false;
    const uint32_t delta = now_utc > event_utc ? now_utc - event_utc : 0;
    unsigned value = 0;
    const char* unit = nullptr;
    if (delta < 60) return written(std::snprintf(buf, size, "%s", pick("just now", "à l'instant")), size);
    if (delta < 3600) {
        value = delta / 60;
        unit = "min";
    } else if (delta < 86400) {
        value = delta / 3600;
        unit = "h";
    } else {
        value = delta / 86400;
        unit = fr() ? "j" : "d";
    }
    if (fr()) return written(std::snprintf(buf, size, "il y a %u %s", value, unit), size);
    return written(std::snprintf(buf, size, "%u %s ago", value, unit), size);
}

bool vigie_format_clock(uint32_t utc, int32_t offset_s, char* buf, size_t size) {
    if (!clear(buf, size) || utc == 0) return false;
    const int64_t t = local_seconds(utc, offset_s);
    const int64_t s = t - floor_div(t, 86400) * 86400;
    return written(std::snprintf(buf, size, "%02d:%02d", static_cast<int>(s / 3600), static_cast<int>((s / 60) % 60)), size);
}

bool vigie_format_date(uint32_t utc, int32_t offset_s, char* buf, size_t size) {
    if (!clear(buf, size) || utc == 0) return false;
    const int64_t days = floor_div(local_seconds(utc, offset_s), 86400);
    int year = 0, month = 1, day = 1;
    civil_from_days(days, year, month, day);
    const int wd = weekday(days);
    if (!fr()) return written(std::snprintf(buf, size, "%s, %s %d", kDaysEn[wd], kMonthsEn[month - 1], day), size);
    if (day == 1) return written(std::snprintf(buf, size, "%s 1er %s", kDaysFr[wd], kMonthsFr[month - 1]), size);
    return written(std::snprintf(buf, size, "%s %d %s", kDaysFr[wd], day, kMonthsFr[month - 1]), size);
}

bool vigie_format_day_short(const char* iso_date, char* buf, size_t size) {
    if (!clear(buf, size) || iso_date == nullptr) return false;
    int year = 0, month = 0, day = 0;
    char extra = 0;
    if (std::sscanf(iso_date, "%4d-%2d-%2d%c", &year, &month, &day, &extra) != 3 || month < 1 || month > 12 ||
        day < 1 || day > 31) {
        return written(std::snprintf(buf, size, "%s", iso_date), size);
    }
    const int wd = weekday(days_from_civil(year, month, day));
    return written(std::snprintf(buf, size, "%s", fr() ? kDaysShortFr[wd] : kDaysShortEn[wd]), size);
}

bool vigie_format_lat_lon(double lat_deg, double lon_deg, char* buf, size_t size) {
    if (!clear(buf, size) || !std::isfinite(lat_deg) || !std::isfinite(lon_deg)) return false;
    char lat[16], lon[16];
    if (!vigie_format_decimal(static_cast<float>(std::fabs(lat_deg)), 2, lat, sizeof(lat)) ||
        !vigie_format_decimal(static_cast<float>(std::fabs(lon_deg)), 2, lon, sizeof(lon))) {
        return false;
    }
    const char* west = fr() ? "O" : "W";
    return written(std::snprintf(buf, size, "%s\xC2\xB0 %s \xC2\xB7 %s\xC2\xB0 %s", lat, lat_deg >= 0.0 ? "N" : "S", lon,
                                 lon_deg >= 0.0 ? "E" : west),
                   size);
}

bool vigie_format_hazard_value(const VigieHazard& hazard, char* buf, size_t size) {
    if (!clear(buf, size) || !std::isfinite(hazard.value) || hazard.value <= 0.0f) return false;
    char number[16];
    switch (hazard.type) {
        case VigieHazardType::EARTHQUAKE:
            if (!vigie_format_decimal(hazard.value, 1, number, sizeof(number))) return false;
            return written(std::snprintf(buf, size, "M %s", number), size);
        case VigieHazardType::SOLAR_STORM:
            if (!vigie_format_decimal(hazard.value, 0, number, sizeof(number))) return false;
            return written(std::snprintf(buf, size, "Kp %s", number), size);
        default:
            return false;
    }
}

bool vigie_is_night(uint32_t utc, int32_t offset_s) {
    if (utc == 0) return false;
    const int64_t t = local_seconds(utc, offset_s);
    const int64_t hour = (t - floor_div(t, 86400) * 86400) / 3600;
    return hour < 7 || hour >= 20;
}

// ─── Weather texts ──────────────────────────────────────────────────────────

bool vigie_format_temperature(float celsius, char* buf, size_t size) {
    if (!clear(buf, size) || !vigie_known(celsius)) return false;
    // An integer has no negative zero: -0.4 °C reads "0°".
    return written(std::snprintf(buf, size, "%ld\xC2\xB0", std::lround(static_cast<double>(celsius))), size);
}

bool vigie_format_speed(float kmh, char* buf, size_t size) {
    if (!clear(buf, size) || !vigie_known(kmh) || kmh < 0.0f) return false;
    return written(std::snprintf(buf, size, "%ld km/h", std::lround(static_cast<double>(kmh))), size);
}

bool vigie_format_wind(float kmh, uint16_t from_deg, char* buf, size_t size) {
    char speed[24];
    if (!clear(buf, size) || !vigie_format_speed(kmh, speed, sizeof(speed))) return false;
    if (from_deg == kVigieUnknownCode) return written(std::snprintf(buf, size, "%s", speed), size);
    return written(std::snprintf(buf, size, "%s %s", speed, vigie_compass_point(from_deg, fr())), size);
}

bool vigie_format_percent(uint8_t pct, char* buf, size_t size) {
    if (!clear(buf, size) || pct > 100) return false;
    return written(std::snprintf(buf, size, fr() ? "%u %%" : "%u%%", static_cast<unsigned>(pct)), size);
}

bool vigie_format_condition(const VigieZone& zone, char* buf, size_t size) {
    if (!clear(buf, size)) return false;
    const char* label = vigie_weather_label(zone.weather_code);
    char temperature[16];
    char feels[48] = "";
    if (vigie_format_temperature(zone.feels_like_c, temperature, sizeof(temperature)) &&
        !written(std::snprintf(feels, sizeof(feels), vigie_tr(VigieText::FEELS_LIKE), temperature), sizeof(feels))) {
        feels[0] = '\0';
    }
    if (label[0] != '\0' && feels[0] != '\0') {
        return written(std::snprintf(buf, size, "%s \xC2\xB7 %s", label, feels), size);
    }
    if (label[0] == '\0' && feels[0] == '\0') return false;
    return written(std::snprintf(buf, size, "%s", label[0] != '\0' ? label : feels), size);
}

bool vigie_format_rain_next_hour(const VigieRainNowcast& rain, char* buf, size_t size) {
    if (!clear(buf, size) || !rain.known) return false;
    if (!rain.expected) return written(std::snprintf(buf, size, "%s", vigie_tr(VigieText::RAIN_NONE)), size);
    if (rain.starts_in_min <= 0) return written(std::snprintf(buf, size, "%s", vigie_tr(VigieText::RAIN_NOW)), size);
    return written(std::snprintf(buf, size, vigie_tr(VigieText::RAIN_IN_MIN), static_cast<int>(rain.starts_in_min)), size);
}

bool vigie_format_air_quality(uint16_t aqi, char* buf, size_t size) {
    if (!clear(buf, size) || aqi == kVigieUnknownCode) return false;
    return written(std::snprintf(buf, size, "%u \xC2\xB7 %s", static_cast<unsigned>(aqi), vigie_aqi_label(aqi)), size);
}

bool vigie_format_waves(const VigieMarine& marine, char* buf, size_t size) {
    char height[16];
    if (!clear(buf, size) || !marine.valid || !vigie_format_decimal(marine.wave_height_m, 1, height, sizeof(height))) {
        return false;
    }
    if (!vigie_known(marine.wave_period_s)) return written(std::snprintf(buf, size, "%s m", height), size);
    return written(
        std::snprintf(buf, size, "%s m \xC2\xB7 %ld s", height, std::lround(static_cast<double>(marine.wave_period_s))),
        size);
}

bool vigie_format_day_rain(const VigieDayForecast& day, char* buf, size_t size) {
    if (!clear(buf, size)) return false;
    if (day.rain_probability_pct != kVigieUnknownPct) return vigie_format_percent(day.rain_probability_pct, buf, size);
    char amount[16];
    if (!vigie_known(day.rain_mm) || day.rain_mm <= 0.0f || !vigie_format_decimal(day.rain_mm, 1, amount, sizeof(amount))) {
        return false;
    }
    return written(std::snprintf(buf, size, "%s mm", amount), size);
}

// ─── Sea texts ──────────────────────────────────────────────────────────────

const char* vigie_activity_name(VigieActivity activity) {
    const size_t i = static_cast<size_t>(activity);
    if (i >= sizeof(kActivityNames) / sizeof(kActivityNames[0])) return "";
    return pick(kActivityNames[i].en, kActivityNames[i].fr);
}

const char* vigie_verdict_label(VigieVerdict verdict) {
    const size_t i = static_cast<size_t>(verdict);
    if (i >= sizeof(kVerdictLabels) / sizeof(kVerdictLabels[0])) return "";
    return pick(kVerdictLabels[i].en, kVerdictLabels[i].fr);
}

const char* vigie_criterion_label(VigieCriterionId criterion) {
    const size_t i = static_cast<size_t>(criterion);
    if (i >= sizeof(kCriterionLabels) / sizeof(kCriterionLabels[0])) return "";
    return pick(kCriterionLabels[i].en, kCriterionLabels[i].fr);
}

const char* vigie_criterion_state_text(VigieCriterionId criterion, VigieCriterionState state) {
    for (const StateText& t : kStateTexts) {
        if (t.criterion == criterion && t.state == state) return pick(t.en, t.fr);
    }
    return "";
}

bool vigie_format_criterion_value(const VigieCriterion& criterion, char* buf, size_t size) {
    if (!clear(buf, size) || !vigie_known(criterion.value)) return false;
    const long rounded = std::lround(static_cast<double>(criterion.value));
    char decimal[16];
    switch (criterion.unit) {
        case VigieUnit::METRES:
            return vigie_format_decimal(criterion.value, 1, decimal, sizeof(decimal)) &&
                   written(std::snprintf(buf, size, "%s m", decimal), size);
        case VigieUnit::KNOTS:
            return vigie_format_decimal(criterion.value, 1, decimal, sizeof(decimal)) &&
                   written(std::snprintf(buf, size, "%s kn", decimal), size);
        case VigieUnit::SECONDS: return written(std::snprintf(buf, size, "%ld s", rounded), size);
        case VigieUnit::DEGREES: return written(std::snprintf(buf, size, "%ld\xC2\xB0", rounded), size);
        case VigieUnit::KMH: return written(std::snprintf(buf, size, "%ld km/h", rounded), size);
        case VigieUnit::BEAUFORT: return written(std::snprintf(buf, size, "Force %ld", rounded), size);
        case VigieUnit::MINUTES: return written(std::snprintf(buf, size, "%ld min", rounded), size);
        case VigieUnit::NONE: return written(std::snprintf(buf, size, "%ld", rounded), size);
        default: return false;
    }
}

bool vigie_format_reason(const VigieActivityForecast& activity, char* buf, size_t size) {
    if (!clear(buf, size)) return false;
    const char* state = vigie_criterion_state_text(activity.reason_criterion, activity.reason_state);
    if (state[0] == '\0') return false;
    return written(std::snprintf(buf, size, fr() ? "%s : %s" : "%s: %s", vigie_criterion_label(activity.reason_criterion), state),
                   size);
}

bool vigie_format_slot(uint32_t start_utc, uint32_t end_utc, int32_t offset_s, char* buf, size_t size) {
    if (!clear(buf, size) || start_utc == 0 || end_utc <= start_utc) return false;
    char from[8], to[8];
    if (!vigie_format_clock(start_utc, offset_s, from, sizeof(from)) || !vigie_format_clock(end_utc, offset_s, to, sizeof(to))) {
        return false;
    }
    return written(std::snprintf(buf, size, "%s\xE2\x80\x93%s", from, to), size);
}

bool vigie_format_tide_extreme(const VigieTideExtreme& extreme, int32_t offset_s, char* buf, size_t size) {
    char clock[8], height[16];
    if (!clear(buf, size) || !vigie_format_clock(extreme.utc, offset_s, clock, sizeof(clock))) return false;
    const char* label = vigie_tr(extreme.high ? VigieText::TIDE_HIGH : VigieText::TIDE_LOW);
    if (!vigie_format_decimal(extreme.height_m, 1, height, sizeof(height))) {
        return written(std::snprintf(buf, size, "%s %s", label, clock), size);
    }
    return written(std::snprintf(buf, size, "%s %s \xC2\xB7 %s m", label, clock, height), size);
}

bool vigie_format_tide_coefficient(const VigieTideCoefficient& coefficient, char* buf, size_t size) {
    if (!clear(buf, size) || coefficient.value == 0) return false;
    return written(std::snprintf(buf, size, vigie_tr(VigieText::COEFFICIENT_ESTIMATED), static_cast<unsigned>(coefficient.value)),
                   size);
}

bool vigie_format_warning(const VigieSea& sea, int32_t offset_s, char* buf, size_t size) {
    if (!clear(buf, size) || !sea.valid) return false;
    // Could not be read: say so, which is not "nothing to fear".
    if (!sea.warnings_known) return written(std::snprintf(buf, size, "%s", vigie_tr(VigieText::SEA_WARNINGS_UNKNOWN)), size);
    if (!sea.warning_present || sea.warning_level == VigieSeverity::GREEN) return false;
    const size_t level = static_cast<size_t>(sea.warning_level);
    const char* name = pick(kWarningLevels[level].en, kWarningLevels[level].fr);
    char until[8];
    if (vigie_format_clock(sea.warning_until_utc, offset_s, until, sizeof(until))) {
        return written(std::snprintf(buf, size, vigie_tr(VigieText::SEA_WARNING_UNTIL), name, until), size);
    }
    return written(std::snprintf(buf, size, vigie_tr(VigieText::SEA_WARNING), name), size);
}

bool vigie_format_sea_status(const VigieSea& sea, VigieSeaFreshness freshness, uint32_t now_utc, char* buf, size_t size) {
    if (!clear(buf, size)) return false;
    switch (freshness) {
        case VigieSeaFreshness::NONE: return written(std::snprintf(buf, size, "%s", vigie_tr(VigieText::SEA_UNAVAILABLE)), size);
        case VigieSeaFreshness::EXPIRED: return written(std::snprintf(buf, size, "%s", vigie_tr(VigieText::SEA_EXPIRED)), size);
        case VigieSeaFreshness::STALE: {
            // A provider's last answer is served: say so, whatever the age of the frame.
            if (sea.stale) return written(std::snprintf(buf, size, "%s", vigie_tr(VigieText::SEA_PROVIDER_STALE)), size);
            char age[40];
            if (!vigie_format_age(now_utc, sea.generated_utc, age, sizeof(age))) return false;
            return written(std::snprintf(buf, size, vigie_tr(VigieText::SEA_AGE), age), size);
        }
        default: return false;  // fresh: nothing to say
    }
}
