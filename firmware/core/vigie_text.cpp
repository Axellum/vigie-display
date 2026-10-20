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
