/**
 * @file vigie_screen.cpp
 * @brief Implementation of vigie_screen.h (see the header).
 *
 * Geometry: the 1024×600 layout, scaled per board (see "Geometry" below). Colours are the
 * palette below (the world map image uses the same ocean and land colours,
 * tools/make_world_map.py).
 */
#include "vigie_screen.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "esphome/components/font/font.h"
#include "esphome/components/image/image.h"
#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/core/hal.h"
#include "lvgl.h"
#include "vigie_text.h"

namespace {

// ─── Palette ────────────────────────────────────────────────────────────────
constexpr uint32_t kBackground = 0x0B1016;
constexpr uint32_t kPanel = 0x131B24;
constexpr uint32_t kLine = 0x223040;
constexpr uint32_t kText = 0xE8EDF2;
constexpr uint32_t kTextDim = 0x8796A8;
constexpr uint32_t kAccent = 0x4CC3FF;
constexpr uint32_t kOcean = 0x0E151D;  // = OCEAN of tools/make_world_map.py
constexpr uint32_t kGreen = 0x46A758;
constexpr uint32_t kYellow = 0xFFC53D;
constexpr uint32_t kOrange = 0xF76B15;
constexpr uint32_t kRed = 0xE5484D;

// ─── Geometry ───────────────────────────────────────────────────────────────
// Drawn for 1024×600 (Waveshare 7B). Another board gives its landscape size, its map size
// and a scale in percent (board package: `board_screen_*`, `board_map_*`,
// `board_layout_scale`, passed as build flags): every length is the 1024×600 one times the
// scale, and what sits on a right or bottom edge is placed from that edge, so the map and
// the side panel take the rest. At 100 the result is the 1024×600 layout, pixel for pixel.
#ifndef VIGIE_SCREEN_WIDTH
#define VIGIE_SCREEN_WIDTH 1024
#endif
#ifndef VIGIE_SCREEN_HEIGHT
#define VIGIE_SCREEN_HEIGHT 600
#endif
#ifndef VIGIE_LAYOUT_SCALE
#define VIGIE_LAYOUT_SCALE 100
#endif
constexpr int kScreenW = VIGIE_SCREEN_WIDTH;
constexpr int kScreenH = VIGIE_SCREEN_HEIGHT;
// A 1024×600 length at the scale of this board.
constexpr int sc(int v) { return (v * VIGIE_LAYOUT_SCALE + 50) / 100; }

constexpr int kMargin = sc(12);
constexpr int kMapX = kMargin;
constexpr int kMapY = sc(60);
constexpr int kAlertsY = kMapY + kVigieMapHeight + sc(8);    // 7B: 334
constexpr int kAlertsW = kVigieMapWidth;
constexpr int kAlertsH = kScreenH - kMargin - kAlertsY;      // 7B: 254
constexpr int kSideX = kMapX + kVigieMapWidth + kMargin;     // 7B: 688
constexpr int kSideW = kScreenW - kMargin - kSideX;          // 7B: 324
constexpr int kSideH = kScreenH - kMargin - kMapY;           // 7B: 528
constexpr int kAlertRows = 5;
constexpr int kAlertRowY = sc(46);
constexpr int kAlertRowH = sc(41);
static_assert(kAlertRowY + kAlertRows * kAlertRowH <= kAlertsH, "the alert rows overflow their panel: smaller map or scale");
static_assert(sc(366) + 5 * sc(31) <= kSideH, "the forecast overflows the weather panel: smaller scale");
static_assert(kSideW >= sc(300), "the weather panel is too narrow: smaller map");
constexpr int kDetailRows = 6;
constexpr int kForecastRows = 5;

constexpr const char* kDash = "\xE2\x80\x93";  // en dash: an unknown value in a table cell

struct AlertRow {
    lv_obj_t* bar = nullptr;
    lv_obj_t* icon = nullptr;
    lv_obj_t* title = nullptr;
    lv_obj_t* subtitle = nullptr;
    lv_obj_t* distance = nullptr;
    lv_obj_t* age = nullptr;
};

struct DetailRow {
    lv_obj_t* label = nullptr;
    lv_obj_t* value = nullptr;
};

struct ForecastRow {
    lv_obj_t* day = nullptr;
    lv_obj_t* icon = nullptr;
    lv_obj_t* rain = nullptr;
    lv_obj_t* tmin = nullptr;
    lv_obj_t* tmax = nullptr;
};

struct Screen {
    bool built = false;
    // Status bar
    lv_obj_t* status_dot = nullptr;
    lv_obj_t* status_text = nullptr;
    lv_obj_t* date = nullptr;
    lv_obj_t* clock = nullptr;
    // Map
    lv_obj_t* map = nullptr;
    lv_obj_t* hazard_dots[kVigieMaxHazards] = {};
    lv_obj_t* zone_marks[kVigieMaxZones] = {};
    // Alerts
    lv_obj_t* alerts_title = nullptr;
    lv_obj_t* count_dots[3] = {};
    lv_obj_t* counts[3] = {};
    lv_obj_t* alerts_empty = nullptr;
    AlertRow rows[kAlertRows];
    // Weather panel
    lv_obj_t* side = nullptr;
    lv_obj_t* zone_name = nullptr;
    lv_obj_t* zone_index = nullptr;
    lv_obj_t* zone_coords = nullptr;
    lv_obj_t* weather_icon = nullptr;
    lv_obj_t* temperature = nullptr;
    lv_obj_t* condition = nullptr;
    DetailRow details[kDetailRows];
    lv_obj_t* separator = nullptr;
    ForecastRow forecast[kForecastRows];
    // Last paint, for a repaint after a touch.
    uint32_t last_ms = 0;
    uint32_t last_utc = 0;
};

Screen g_screen;

uint32_t severity_colour(VigieSeverity s) {
    switch (s) {
        case VigieSeverity::RED: return kRed;
        case VigieSeverity::ORANGE: return kOrange;
        case VigieSeverity::YELLOW: return kYellow;
        case VigieSeverity::GREEN: return kGreen;
    }
    return kGreen;
}

int severity_dot_size(VigieSeverity s) {
    switch (s) {
        case VigieSeverity::RED: return sc(16);
        case VigieSeverity::ORANGE: return sc(13);
        case VigieSeverity::YELLOW: return sc(10);
        case VigieSeverity::GREEN: return sc(8);
    }
    return sc(8);
}

// ─── Builders ───────────────────────────────────────────────────────────────

lv_obj_t* box(lv_obj_t* parent, int x, int y, int w, int h, uint32_t colour, int radius, bool filled = true) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(colour), 0);
    lv_obj_set_style_bg_opa(o, filled ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

// One-line label; `w` > 0: fixed width, text cut with "…".
lv_obj_t* label(lv_obj_t* parent, const esphome::font::Font* font, uint32_t colour, int x, int y, int w = 0,
                lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
    lv_obj_t* l = lv_label_create(parent);
    esphome::lvgl::lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(colour), 0);
    lv_obj_set_style_text_align(l, align, 0);
    lv_obj_set_pos(l, x, y);
    if (w > 0) {
        lv_obj_set_size(l, w, lv_font_get_line_height(font->get_lv_font()));
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    }
    lv_label_set_text(l, "");
    return l;
}

// ─── Compare-first writes ───────────────────────────────────────────────────

void set_text(lv_obj_t* l, const char* text) {
    if (l != nullptr && std::strcmp(lv_label_get_text(l), text) != 0) lv_label_set_text(l, text);
}

void set_text_colour(lv_obj_t* o, uint32_t colour) {
    const lv_color_t c = lv_color_hex(colour);
    if (o != nullptr && !lv_color_eq(lv_obj_get_style_text_color(o, LV_PART_MAIN), c)) {
        lv_obj_set_style_text_color(o, c, 0);
    }
}

void set_bg_colour(lv_obj_t* o, uint32_t colour) {
    const lv_color_t c = lv_color_hex(colour);
    if (o != nullptr && !lv_color_eq(lv_obj_get_style_bg_color(o, LV_PART_MAIN), c)) {
        lv_obj_set_style_bg_color(o, c, 0);
    }
}

void set_hidden(lv_obj_t* o, bool hidden) {
    if (o != nullptr && lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) != hidden) lv_obj_set_flag(o, LV_OBJ_FLAG_HIDDEN, hidden);
}

void set_area(lv_obj_t* o, int x, int y, int size) {
    if (lv_obj_get_x(o) != x || lv_obj_get_y(o) != y) lv_obj_set_pos(o, x, y);
    if (lv_obj_get_width(o) != size) lv_obj_set_size(o, size, size);
}

// ─── Painters ───────────────────────────────────────────────────────────────

void paint_status(const VigieWorldState& state, const VigieLinkStatus& link, uint32_t now_ms, uint32_t utc) {
    Screen& s = g_screen;
    char buf[96];
    uint32_t dot = kTextDim;
    switch (vigie_link_state(link, now_ms)) {
        case VigieLinkState::WAITING: dot = kYellow; break;
        case VigieLinkState::UP_TO_DATE: dot = kGreen; break;
        case VigieLinkState::DOWN: dot = kRed; break;
    }
    set_bg_colour(s.status_dot, dot);
    set_text(s.status_text, vigie_format_link_status(link, now_ms, buf, sizeof(buf)) ? buf : "");

    // Clock and date of the active zone; in UTC, said so, when its offset is unknown.
    int32_t offset = 0;
    bool local = false;
    if (state.active_zone >= 0 && state.active_zone < static_cast<int8_t>(state.zone_count)) {
        local = state.zones[state.active_zone].utc_offset_known;
        if (local) offset = state.zones[state.active_zone].utc_offset_s;
    }
    set_text(s.clock, vigie_format_clock(utc, offset, buf, sizeof(buf)) ? buf : "--:--");
    if (vigie_format_date(utc, offset, buf, sizeof(buf))) {
        // The clock label is too narrow for a suffix: the date says "UTC".
        if (!local) std::strncat(buf, " \xC2\xB7 UTC", sizeof(buf) - std::strlen(buf) - 1);
        set_text(s.date, buf);
    } else {
        set_text(s.date, "");
    }
}

void paint_map(const VigieWorldState& state) {
    Screen& s = g_screen;
    const int n = state.hazard_count <= kVigieMaxHazards ? state.hazard_count : kVigieMaxHazards;
    // Dots are drawn in creation order: the least severe hazards get the first objects,
    // so that the most severe end on top.
    for (int j = 0; j < kVigieMaxHazards; ++j) {
        lv_obj_t* dot = s.hazard_dots[j];
        const int i = n - 1 - j;
        if (i < 0) {
            set_hidden(dot, true);
            continue;
        }
        const VigieHazard& h = state.hazards[i];
        const VigiePixel p = vigie_map_project(h.lat, h.lon, kVigieMapWidth, kVigieMapHeight);
        if (!p.inside) {
            set_hidden(dot, true);
            continue;
        }
        const int size = severity_dot_size(h.severity);
        set_area(dot, p.x - size / 2, p.y - size / 2, size);
        set_bg_colour(dot, severity_colour(h.severity));
        const int outline = h.severity == VigieSeverity::RED ? sc(4) : 0;
        if (lv_obj_get_style_outline_width(dot, LV_PART_MAIN) != outline) lv_obj_set_style_outline_width(dot, outline, 0);
        set_hidden(dot, false);
    }

    const int zones = state.zone_count <= kVigieMaxZones ? state.zone_count : kVigieMaxZones;
    for (int i = 0; i < kVigieMaxZones; ++i) {
        lv_obj_t* mark = s.zone_marks[i];
        if (i >= zones) {
            set_hidden(mark, true);
            continue;
        }
        const VigiePixel p = vigie_map_project(state.zones[i].lat, state.zones[i].lon, kVigieMapWidth, kVigieMapHeight);
        const bool active = i == state.active_zone;
        const int size = active ? sc(14) : sc(6);
        set_area(mark, p.x - size / 2, p.y - size / 2, size);
        const lv_opa_t opa = active ? LV_OPA_TRANSP : LV_OPA_COVER;
        if (lv_obj_get_style_bg_opa(mark, LV_PART_MAIN) != opa) lv_obj_set_style_bg_opa(mark, opa, 0);
        const int border = active ? sc(2) : 0;
        if (lv_obj_get_style_border_width(mark, LV_PART_MAIN) != border) lv_obj_set_style_border_width(mark, border, 0);
        set_hidden(mark, !p.inside);
    }
}

void paint_alerts(const VigieWorldState& state, uint32_t utc) {
    Screen& s = g_screen;
    char buf[128];
    set_text(s.alerts_title, vigie_tr(VigieText::ACTIVE_ALERTS));
    const uint16_t counts[3] = {state.red_count, state.orange_count, state.yellow_count};
    for (int k = 0; k < 3; ++k) {
        std::snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(counts[k]));
        set_text(s.counts[k], state.hazards_known ? buf : kDash);
        set_text_colour(s.counts[k], state.hazards_known && counts[k] > 0 ? kText : kTextDim);
    }

    const int n = state.hazard_count <= kVigieMaxHazards ? state.hazard_count : kVigieMaxHazards;
    set_hidden(s.alerts_empty, n > 0);
    VigieText empty = VigieText::NO_ACTIVE_ALERT;
    if (state.updated_utc == 0) {
        empty = VigieText::WAITING_DATA;
    } else if (!state.hazards_known) {
        empty = VigieText::ALERTS_UNKNOWN;  // no source answered: never "no active alert"
    }
    set_text(s.alerts_empty, vigie_tr(empty));
    const bool french = vigie_language() == VigieLanguage::FR;
    for (int r = 0; r < kAlertRows; ++r) {
        AlertRow& row = s.rows[r];
        const bool used = r < n;
        set_hidden(row.bar, !used);
        set_hidden(row.icon, !used);
        set_hidden(row.title, !used);
        set_hidden(row.subtitle, !used);
        set_hidden(row.distance, !used);
        set_hidden(row.age, !used);
        if (!used) continue;
        const VigieHazard& h = state.hazards[r];
        const uint32_t colour = severity_colour(h.severity);
        set_bg_colour(row.bar, colour);
        set_text(row.icon, vigie_hazard_icon(h.type));
        set_text_colour(row.icon, colour);

        char value[24];
        if (vigie_format_hazard_value(h, value, sizeof(value))) {
            std::snprintf(buf, sizeof(buf), "%s \xC2\xB7 %s", vigie_hazard_name(h.type), value);
        } else {
            std::snprintf(buf, sizeof(buf), "%s", vigie_hazard_name(h.type));
        }
        set_text(row.title, buf);
        set_text(row.subtitle, h.title);

        char distance[24];
        if (state.zone_count > 0 && vigie_format_distance(h.distance_km, distance, sizeof(distance))) {
            std::snprintf(buf, sizeof(buf), "%s %s", distance, vigie_compass_point(h.bearing_deg, french));
        } else {
            buf[0] = '\0';
        }
        set_text(row.distance, buf);
        set_text(row.age, vigie_format_age(utc, h.timestamp_utc, buf, sizeof(buf)) ? buf : "");
    }
}

void set_detail(int i, VigieText name, const char* value) {
    DetailRow& d = g_screen.details[i];
    set_text(d.label, vigie_tr(name));
    set_text(d.value, value);
    set_hidden(d.label, false);
    set_hidden(d.value, false);
}

void hide_detail(int i) {
    set_hidden(g_screen.details[i].label, true);
    set_hidden(g_screen.details[i].value, true);
}

void paint_weather(const VigieWorldState& state, uint32_t utc) {
    Screen& s = g_screen;
    char buf[96];
    const bool has_zone = state.active_zone >= 0 && state.active_zone < static_cast<int8_t>(state.zone_count);
    if (!has_zone) {
        set_text(s.zone_name, vigie_tr(VigieText::WAITING_DATA));
        set_text(s.zone_index, "");
        set_text(s.zone_coords, "");
    } else {
        const VigieZone& z = state.zones[state.active_zone];
        set_text(s.zone_name, z.name);
        std::snprintf(buf, sizeof(buf), "%d/%u", state.active_zone + 1, static_cast<unsigned>(state.zone_count));
        set_text(s.zone_index, state.zone_count > 1 ? buf : "");
        set_text(s.zone_coords, vigie_format_lat_lon(z.lat, z.lon, buf, sizeof(buf)) ? buf : "");
    }
    const bool weather = has_zone && state.zones[state.active_zone].has_weather;
    set_hidden(s.separator, !weather);
    if (!weather) {
        set_hidden(s.weather_icon, true);
        set_hidden(s.temperature, true);
        set_text(s.condition, has_zone ? vigie_tr(VigieText::NO_WEATHER) : "");
        for (int i = 0; i < kDetailRows; ++i) hide_detail(i);
        for (int i = 0; i < kForecastRows; ++i) {
            ForecastRow& f = s.forecast[i];
            set_hidden(f.day, true);
            set_hidden(f.icon, true);
            set_hidden(f.rain, true);
            set_hidden(f.tmin, true);
            set_hidden(f.tmax, true);
        }
        return;
    }

    // Every value comes from a vigie_text formatter, which returns false when it is unknown:
    // an unknown value is hidden, never shown as 0.
    const VigieZone& z = state.zones[state.active_zone];
    const char* icon = vigie_weather_icon(z.weather_code, vigie_is_night(utc, z.utc_offset_s));
    set_text(s.weather_icon, icon);
    set_hidden(s.weather_icon, icon[0] == '\0');
    const bool temperature = vigie_format_temperature(z.temp_c, buf, sizeof(buf));
    set_text(s.temperature, buf);
    set_hidden(s.temperature, !temperature);
    set_text(s.condition, vigie_format_condition(z, buf, sizeof(buf)) ? buf : "");

    int row = 0;
    if (vigie_format_wind(z.wind_kmh, z.wind_deg, buf, sizeof(buf))) set_detail(row++, VigieText::WIND, buf);
    if (vigie_format_speed(z.gust_kmh, buf, sizeof(buf))) set_detail(row++, VigieText::GUSTS, buf);
    if (vigie_format_percent(z.humidity_pct, buf, sizeof(buf))) set_detail(row++, VigieText::HUMIDITY, buf);
    if (vigie_format_rain_next_hour(z.rain, buf, sizeof(buf))) set_detail(row++, VigieText::RAIN_NEXT_HOUR, buf);
    if (vigie_format_air_quality(z.aqi, buf, sizeof(buf))) set_detail(row++, VigieText::AIR_QUALITY, buf);
    if (vigie_format_waves(z.marine, buf, sizeof(buf))) set_detail(row++, VigieText::WAVES, buf);
    while (row < kDetailRows) hide_detail(row++);

    // Days: the first forecast day is today; the panel shows the next five days with data.
    for (int i = 0; i < kForecastRows; ++i) {
        ForecastRow& f = s.forecast[i];
        const bool used = i < z.day_count;
        set_hidden(f.day, !used);
        set_hidden(f.icon, !used);
        set_hidden(f.rain, !used);
        set_hidden(f.tmin, !used);
        set_hidden(f.tmax, !used);
        if (!used) continue;
        const VigieDayForecast& d = z.days[i];
        set_text(f.day, vigie_format_day_short(d.label, buf, sizeof(buf)) ? buf : d.label);
        set_text(f.icon, vigie_weather_icon(d.weather_code, false));
        set_text(f.rain, vigie_format_day_rain(d, buf, sizeof(buf)) ? buf : "");
        // In a table an unknown temperature reads as a dash, so that the columns stay aligned.
        set_text(f.tmin, vigie_format_temperature(d.tmin_c, buf, sizeof(buf)) ? buf : kDash);
        set_text(f.tmax, vigie_format_temperature(d.tmax_c, buf, sizeof(buf)) ? buf : kDash);
    }
}

void on_side_clicked(lv_event_t*) {
    vigie_next_zone(g_vigie_state);
    const uint32_t now = esphome::millis();
    const uint32_t utc = g_screen.last_utc == 0 ? 0 : g_screen.last_utc + (now - g_screen.last_ms) / 1000;
    vigie_screen_paint(g_vigie_state, g_vigie_link, now, utc);
}

}  // namespace

void vigie_screen_build(const VigieScreenFonts& f, esphome::image::Image* world_map) {
    Screen& s = g_screen;
    if (s.built) return;
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(kBackground), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // Status bar (clock and date from the right edge).
    lv_obj_t* globe = label(scr, f.icons, kAccent, sc(16), sc(14));
    lv_label_set_text(globe, "\U000F01E7");  // earth
    lv_obj_t* brand = label(scr, f.body_bold, kText, sc(48), sc(15));
    lv_obj_set_style_text_letter_space(brand, sc(2), 0);
    lv_label_set_text(brand, "VIGIE");
    s.status_dot = box(scr, sc(128), sc(22), sc(10), sc(10), kTextDim, LV_RADIUS_CIRCLE);
    s.status_text = label(scr, f.small, kTextDim, sc(146), sc(17), sc(420));
    const int clock_x = kScreenW - kMargin - sc(100);                // 7B: 912
    const int date_x = clock_x - sc(12) - sc(340);                   // 7B: 560
    s.date = label(scr, f.body, kTextDim, date_x, sc(15), sc(340), LV_TEXT_ALIGN_RIGHT);
    s.clock = label(scr, f.title, kText, clock_x, sc(11), sc(100), LV_TEXT_ALIGN_RIGHT);

    // Map.
    s.map = box(scr, kMapX, kMapY, kVigieMapWidth, kVigieMapHeight, kOcean, sc(10));
    lv_obj_set_style_clip_corner(s.map, true, 0);
    if (world_map != nullptr) {
        lv_obj_t* img = lv_image_create(s.map);
        esphome::lvgl::lv_image_set_src(img, world_map);
        lv_obj_set_pos(img, 0, 0);
    }
    for (auto& dot : s.hazard_dots) {
        dot = box(s.map, 0, 0, sc(8), sc(8), kGreen, LV_RADIUS_CIRCLE);
        lv_obj_set_style_border_width(dot, sc(2), 0);
        lv_obj_set_style_border_color(dot, lv_color_hex(kOcean), 0);
        lv_obj_set_style_outline_color(dot, lv_color_hex(kRed), 0);
        lv_obj_set_style_outline_opa(dot, LV_OPA_40, 0);
        lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
    }
    for (auto& mark : s.zone_marks) {
        mark = box(s.map, 0, 0, sc(6), sc(6), kAccent, LV_RADIUS_CIRCLE);
        lv_obj_set_style_border_color(mark, lv_color_hex(kText), 0);
        lv_obj_add_flag(mark, LV_OBJ_FLAG_HIDDEN);
    }

    // Alerts.
    lv_obj_t* alerts = box(scr, kMapX, kAlertsY, kAlertsW, kAlertsH, kPanel, sc(10));
    const int pad = sc(16);  // inner margin of the panels
    s.alerts_title = label(alerts, f.body_bold, kText, pad, sc(12), sc(300));
    const uint32_t count_colours[3] = {kRed, kOrange, kYellow};
    for (int k = 0; k < 3; ++k) {
        const int gx = kAlertsW - pad - sc(60) * (3 - k);
        s.count_dots[k] = box(alerts, gx + sc(8), sc(19), sc(10), sc(10), count_colours[k], LV_RADIUS_CIRCLE);
        s.counts[k] = label(alerts, f.body, kTextDim, gx + sc(24), sc(12), sc(36));
    }
    s.alerts_empty = label(alerts, f.body, kTextDim, pad, sc(120), kAlertsW - 2 * pad, LV_TEXT_ALIGN_CENTER);
    // Distance and age on the right edge; the title takes the rest.
    const int distance_w = sc(176);
    const int distance_x = kAlertsW - pad - distance_w;            // 7B: 472
    const int title_x = sc(64);
    const int title_w = distance_x - sc(8) - title_x;              // 7B: 400
    for (int r = 0; r < kAlertRows; ++r) {
        AlertRow& row = s.rows[r];
        const int y = kAlertRowY + r * kAlertRowH;
        row.bar = box(alerts, pad, y + sc(7), sc(4), sc(27), kGreen, sc(2));
        row.icon = label(alerts, f.icons, kText, sc(28), y + sc(8), sc(30));
        row.title = label(alerts, f.body_bold, kText, title_x, y + sc(1), title_w);
        row.subtitle = label(alerts, f.small, kTextDim, title_x, y + sc(23), title_w);
        row.distance = label(alerts, f.body, kText, distance_x, y + sc(1), distance_w, LV_TEXT_ALIGN_RIGHT);
        row.age = label(alerts, f.small, kTextDim, distance_x, y + sc(23), distance_w, LV_TEXT_ALIGN_RIGHT);
    }

    // Weather of the active zone (touch: next zone). Right-aligned columns from the right edge.
    s.side = box(scr, kSideX, kMapY, kSideW, kSideH, kPanel, sc(10));
    lv_obj_add_flag(s.side, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s.side, on_side_clicked, LV_EVENT_CLICKED, nullptr);
    const int chevron_x = kSideW - sc(34);                          // 7B: 290
    const int index_x = chevron_x - sc(46);                         // 7B: 244
    s.zone_name = label(s.side, f.title, kText, pad, sc(12), index_x - sc(2) - pad);
    s.zone_index = label(s.side, f.small, kTextDim, index_x, sc(18), sc(44), LV_TEXT_ALIGN_RIGHT);
    lv_obj_t* chevron = label(s.side, f.icons, kAccent, chevron_x, sc(14));
    lv_label_set_text(chevron, "\U000F0142");  // chevron-right
    s.zone_coords = label(s.side, f.small, kTextDim, pad, sc(46), kSideW - 2 * pad);
    s.weather_icon = label(s.side, f.weather_icon, kText, sc(12), sc(70));
    s.temperature = label(s.side, f.temperature, kText, sc(100), sc(66));
    s.condition = label(s.side, f.body, kText, pad, sc(150), kSideW - 2 * pad);
    for (int i = 0; i < kDetailRows; ++i) {
        const int y = sc(186) + i * sc(28);
        s.details[i].label = label(s.side, f.small, kTextDim, pad, y + sc(3), sc(140));
        s.details[i].value = label(s.side, f.body, kText, sc(120), y, kSideW - pad - sc(120), LV_TEXT_ALIGN_RIGHT);
    }
    s.separator = box(s.side, pad, sc(356), kSideW - 2 * pad, 1, kLine, 0);
    const int tmax_x = kSideW - pad - sc(58);                       // 7B: 250
    const int tmin_x = tmax_x - sc(6) - sc(56);                     // 7B: 188
    for (int i = 0; i < kForecastRows; ++i) {
        ForecastRow& row = s.forecast[i];
        const int y = sc(366) + i * sc(31);
        row.day = label(s.side, f.body_bold, kText, pad, y, sc(64));
        row.icon = label(s.side, f.icons, kText, sc(84), y, sc(30));
        row.rain = label(s.side, f.small, kTextDim, sc(118), y + sc(3), tmin_x - sc(118));
        row.tmin = label(s.side, f.body, kTextDim, tmin_x, y, sc(56), LV_TEXT_ALIGN_RIGHT);
        row.tmax = label(s.side, f.body_bold, kText, tmax_x, y, sc(58), LV_TEXT_ALIGN_RIGHT);
    }
    s.built = true;
}

void vigie_screen_paint(const VigieWorldState& state, const VigieLinkStatus& link, uint32_t now_ms, uint32_t utc) {
    if (!g_screen.built) return;
    g_screen.last_ms = now_ms;
    g_screen.last_utc = utc;
    paint_status(state, link, now_ms, utc);
    paint_map(state);
    paint_alerts(state, utc);
    paint_weather(state, utc);
}
