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
constexpr uint32_t kWhite = 0xFFFFFF;  // the "now" line of the sea page

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

// The sea page (second page, drawn when vigie_sea.h has the sea of a spot): a banner under the title
// bar, four activity cards, a legend, then the tide and the sun. Same 1024×600 numbers through sc().
constexpr int kSeaGap = sc(10);
constexpr int kSeaBannerY = sc(58);
constexpr int kSeaBannerH = sc(38);
constexpr int kSeaCardsY = sc(104);
constexpr int kSeaCardH = sc(300);
constexpr int kSeaCardW = (kScreenW - 2 * kMargin - 3 * kSeaGap) / 4;  // 7B: 242
constexpr int kSeaCardPad = sc(14);
constexpr int kSeaCardInner = kSeaCardW - 2 * kSeaCardPad;
constexpr int kSeaBarPitch = kSeaCardInner / kVigieSeaHours;  // 7B: 8 px per hour
constexpr int kSeaStripW = kSeaBarPitch * kVigieSeaHours;
constexpr int kSeaStripX = (kSeaCardW - kSeaStripW) / 2;
constexpr int kSeaStripY = sc(168);
constexpr int kSeaStripH = sc(30);
constexpr int kSeaRows = 3;       // criteria shown on a card
constexpr int kSeaRowsY = sc(226);  // 228 would overflow by a pixel at 120 % (each length is rounded)
constexpr int kSeaRowH = sc(24);
constexpr int kSeaValueW = sc(84);                      // "Force 6" in bold needs 70 px at 100 %
constexpr int kSeaLabelW = kSeaCardInner - sc(60);      // overlaps the value's box, not its right-aligned text
constexpr int kSeaLegendY = kSeaCardsY + kSeaCardH + sc(8);
constexpr int kSeaBottomY = kSeaLegendY + sc(26);
constexpr int kSeaBottomH = kScreenH - kMargin - kSeaBottomY;  // 7B: 150
constexpr int kSeaTideW = (kScreenW - 2 * kMargin - kSeaGap) * 64 / 100;
constexpr int kSeaSunW = kScreenW - 2 * kMargin - kSeaGap - kSeaTideW;
constexpr int kTideSamples = 49;  // every half hour of the 24 hours of the strips
constexpr int kTideCurveX = sc(16);
constexpr int kTideCurveY = sc(42);
constexpr int kTideCurveW = kSeaTideW - 2 * kTideCurveX;
constexpr int kTideCurveH = kSeaBottomH - kTideCurveY - sc(36);
constexpr int kDetailBlocks = 4;  // most points a criterion gives is drawn as blocks (surf: 2)
static_assert(kSeaBarPitch >= 6, "the hour bars are too thin: smaller scale or wider cards");
static_assert(kSeaRowsY + kSeaRows * kSeaRowH <= kSeaCardH, "the criteria rows overflow their card");
static_assert(kSeaBottomH >= sc(140), "the tide panel is too short: the title bar and the cards take too much");
static_assert(kTideCurveH >= sc(50), "the tide curve has no room");

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

// One activity card of the sea page.
struct SeaCard {
    int index = 0;  // VigieActivity, given to the touch handler
    lv_obj_t* box = nullptr;
    lv_obj_t* icon = nullptr;
    lv_obj_t* name = nullptr;
    lv_obj_t* verdict = nullptr;
    lv_obj_t* reason = nullptr;
    lv_obj_t* slot_label = nullptr;
    lv_obj_t* slot = nullptr;
    lv_obj_t* bars[kVigieSeaHours] = {};
    lv_obj_t* now = nullptr;
    lv_obj_t* axis[3] = {};
    lv_obj_t* row_label[kSeaRows] = {};
    lv_obj_t* row_value[kSeaRows] = {};
};

// One criterion of the detail popup.
struct CriterionRow {
    lv_obj_t* label = nullptr;
    lv_obj_t* value = nullptr;
    lv_obj_t* blocks[kDetailBlocks] = {};
    lv_obj_t* score = nullptr;
    lv_obj_t* dot = nullptr;
    lv_obj_t* reading = nullptr;
};

enum class Page : uint8_t { WORLD = 0, SEA = 1 };

struct Screen {
    bool built = false;
    Page page = Page::WORLD;
    int detail_activity = -1;  // the activity of the open detail popup; -1 closed
    // Status bar
    lv_obj_t* brand = nullptr;  // "VIGIE", or the name of the spot on the sea page
    lv_obj_t* status_dot = nullptr;
    lv_obj_t* status_text = nullptr;
    lv_obj_t* date = nullptr;
    lv_obj_t* clock = nullptr;
    lv_obj_t* page_switch = nullptr;  // the pill that goes to the other page
    lv_obj_t* page_switch_text = nullptr;
    lv_obj_t* page_switch_icon = nullptr;
    // The two pages
    lv_obj_t* world_page = nullptr;
    lv_obj_t* sea_page = nullptr;
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
    // Sea page: banner, cards, legend, tide, sun
    lv_obj_t* banner = nullptr;
    lv_obj_t* banner_icon = nullptr;
    lv_obj_t* banner_text = nullptr;
    SeaCard cards[static_cast<int>(VigieActivity::COUNT_)];
    lv_obj_t* legend_swatch[4] = {};
    lv_obj_t* legend_label[4] = {};
    lv_obj_t* legend_hint = nullptr;
    lv_obj_t* tide_panel = nullptr;
    lv_obj_t* sun_panel = nullptr;
    lv_obj_t* tide_title = nullptr;
    lv_obj_t* tide_badge = nullptr;
    lv_obj_t* tide_badge_text = nullptr;
    lv_obj_t* tide_line = nullptr;
    lv_obj_t* tide_now_line = nullptr;
    lv_obj_t* tide_now_dot = nullptr;
    lv_obj_t* tide_empty = nullptr;
    lv_obj_t* tide_next[2] = {};
    lv_point_precise_t tide_points[kTideSamples] = {};  // lv_line does not copy them: they live here
    int tide_point_count = 0;
    lv_obj_t* sun_title = nullptr;
    lv_obj_t* sun_label[2] = {};
    lv_obj_t* sun_value[2] = {};
    lv_obj_t* sea_notice = nullptr;
    // Detail popup of an activity (the criteria behind its verdict)
    lv_obj_t* detail = nullptr;  // the scrim, which closes it when touched
    lv_obj_t* detail_icon = nullptr;
    lv_obj_t* detail_title = nullptr;
    lv_obj_t* detail_subtitle = nullptr;
    lv_obj_t* detail_verdict = nullptr;
    lv_obj_t* detail_score = nullptr;
    lv_obj_t* detail_heads[4] = {};
    CriterionRow criteria[kVigieMaxCriteria];
    lv_obj_t* detail_notice = nullptr;
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

void set_pos(lv_obj_t* o, int x, int y) {
    if (o != nullptr && (lv_obj_get_x(o) != x || lv_obj_get_y(o) != y)) lv_obj_set_pos(o, x, y);
}

void set_width(lv_obj_t* o, int w) {
    if (o != nullptr && lv_obj_get_width(o) != w) lv_obj_set_width(o, w);
}

void set_opa(lv_obj_t* o, lv_opa_t opa) {
    if (o != nullptr && lv_obj_get_style_opa(o, LV_PART_MAIN) != opa) lv_obj_set_style_opa(o, opa, 0);
}

void set_bg_opa(lv_obj_t* o, lv_opa_t opa) {
    if (o != nullptr && lv_obj_get_style_bg_opa(o, LV_PART_MAIN) != opa) lv_obj_set_style_bg_opa(o, opa, 0);
}

void set_border_colour(lv_obj_t* o, uint32_t colour) {
    const lv_color_t c = lv_color_hex(colour);
    if (o != nullptr && !lv_color_eq(lv_obj_get_style_border_color(o, LV_PART_MAIN), c)) {
        lv_obj_set_style_border_color(o, c, 0);
    }
}

void set_flag(lv_obj_t* o, lv_obj_flag_t flag, bool on) {
    if (o != nullptr && lv_obj_has_flag(o, flag) != on) lv_obj_set_flag(o, flag, on);
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

// ─── Sea page ───────────────────────────────────────────────────────────────

uint32_t verdict_colour(VigieVerdict v) {
    switch (v) {
        case VigieVerdict::GOOD: return kGreen;
        case VigieVerdict::POSSIBLE: return kYellow;
        case VigieVerdict::POOR: return kRed;
        case VigieVerdict::UNKNOWN: break;
    }
    return kTextDim;
}

const char* activity_icon(int activity) {
    switch (activity) {
        case 0: return "\U000F078D";  // waves (surf)
        case 1: return "\U000F1744";  // kitesurfing
        case 2: return "\U000F0EC8";  // sail-boat
        case 3: return "\U000F1417";  // pail (shore fishing)
    }
    return "";
}

// The times of the sea are shown in the clock of the zone on screen (the frame has no offset of the spot).
int32_t zone_offset(const VigieWorldState& state) {
    if (state.active_zone >= 0 && state.active_zone < static_cast<int8_t>(state.zone_count) &&
        state.zones[state.active_zone].utc_offset_known) {
        return state.zones[state.active_zone].utc_offset_s;
    }
    return 0;
}

// An activity whose verdict is shown: it has one, and the sea is neither missing nor too old.
bool judged(const VigieActivityForecast& a, bool show) {
    return show && a.present && a.available && a.verdict != VigieVerdict::UNKNOWN;
}

void paint_banner(VigieSeaBanner kind, const VigieSea& sea, const char* text) {
    Screen& s = g_screen;
    // Nothing to warn about: a quiet line, the nearest town, so that the top of the page is not an empty band.
    const bool quiet = kind == VigieSeaBanner::NONE;
    set_hidden(s.banner, quiet && sea.place[0] == '\0');
    set_hidden(s.banner_icon, quiet);
    set_pos(s.banner_text, quiet ? sc(2) : sc(46), sc(7));
    if (quiet) {
        set_bg_opa(s.banner, LV_OPA_TRANSP);
        set_border_colour(s.banner, kBackground);
        set_text_colour(s.banner_text, kTextDim);
        set_text(s.banner_text, sea.place);
        return;
    }
    uint32_t accent = kTextDim;
    uint32_t border = kLine;
    uint32_t text_colour = kText;
    bool tinted = false;
    switch (kind) {
        case VigieSeaBanner::WARNING:
            accent = severity_colour(sea.warning_level);
            border = accent;
            tinted = true;
            break;
        case VigieSeaBanner::WARNING_UNKNOWN:  // never shown as an all-clear: it is asked to be noticed
            accent = kYellow;
            border = kYellow;
            break;
        case VigieSeaBanner::STATUS:
        case VigieSeaBanner::NONE:
            text_colour = kTextDim;
            break;
    }
    set_bg_colour(s.banner, tinted ? accent : kPanel);
    set_bg_opa(s.banner, tinted ? LV_OPA_20 : LV_OPA_COVER);
    set_border_colour(s.banner, border);
    set_text_colour(s.banner_icon, accent);
    set_text_colour(s.banner_text, text_colour);
    set_text(s.banner_text, text);
}

void paint_card(int i, const VigieSea& sea, bool show, bool dim, int32_t offset, uint32_t utc) {
    SeaCard& c = g_screen.cards[i];
    const VigieActivityForecast& a = sea.activities[i];
    char buf[96];
    set_text(c.name, vigie_activity_name(static_cast<VigieActivity>(i)));
    set_opa(c.box, dim ? LV_OPA_60 : LV_OPA_COVER);

    const bool shown = judged(a, show);
    const uint32_t colour = shown ? verdict_colour(a.verdict) : kTextDim;
    set_text_colour(c.icon, colour);
    set_text_colour(c.verdict, colour);
    set_text(c.verdict, shown ? vigie_verdict_label(a.verdict) : kDash);
    // A card with criteria opens their detail; one without has nothing to open.
    set_flag(c.box, LV_OBJ_FLAG_CLICKABLE, shown && a.criterion_count > 0);

    // Why it is not good, or that it cannot be judged here.
    const char* reason = "";
    if (!show) {
        reason = vigie_tr(VigieText::SEA_EXPIRED);  // no verdict: say why on the card itself
    } else if (a.present && !a.available) {
        reason = vigie_tr(VigieText::SEA_NOT_AVAILABLE);
    } else if (shown && vigie_format_reason(a, buf, sizeof(buf))) {
        reason = buf;
    }
    set_text(c.reason, reason);

    // Best slot.
    set_hidden(c.slot_label, !shown);
    set_hidden(c.slot, !shown);
    if (shown) {
        set_text(c.slot_label, vigie_tr(VigieText::SEA_BEST_SLOT));
        char slot[32];
        set_text(c.slot, vigie_format_slot(a.slot_start_utc, a.slot_end_utc, offset, slot, sizeof(slot))
                             ? slot
                             : vigie_tr(VigieText::SEA_NO_SLOT));
    }

    // Hours: one bar each, the colour of its verdict by day, dark by night.
    for (int h = 0; h < kVigieSeaHours; ++h) {
        lv_obj_t* bar = c.bars[h];
        set_hidden(bar, !shown);
        if (!shown) continue;
        const bool night = a.daylight[h] == VigieDaylight::NIGHT;
        const uint32_t bar_colour = (night || a.hours[h] == VigieVerdict::UNKNOWN) ? kLine : verdict_colour(a.hours[h]);
        set_bg_colour(bar, bar_colour);
    }
    const int now_index = shown ? vigie_sea_hour_index(sea, utc) : -1;
    set_hidden(c.now, now_index < 0);
    if (now_index >= 0) {
        set_pos(c.now, kSeaStripX + now_index * kSeaBarPitch + kSeaBarPitch / 2 - 1, kSeaStripY - sc(4));
    }
    // Axis: the clock at the start of the strip, 12 hours later, and at its end.
    const bool axis = shown && sea.first_hour_utc != 0;
    for (int k = 0; k < 3; ++k) {
        set_hidden(c.axis[k], !axis);
        if (axis) {
            char clock[8];
            const uint32_t at = sea.first_hour_utc + static_cast<uint32_t>(k) * 12u * 3600u;
            set_text(c.axis[k], vigie_format_clock(at, offset, clock, sizeof(clock)) ? clock : "");
        }
    }

    // The first criteria whose value is known (the detail has them all). Unknown is not shown.
    int row = 0;
    if (shown) {
        for (int k = 0; k < a.criterion_count && row < kSeaRows; ++k) {
            const VigieCriterion& cr = a.criteria[k];
            if (cr.id == VigieCriterionId::VIGILANCE) continue;
            char value[24];
            if (!vigie_format_criterion_value(cr, value, sizeof(value))) continue;
            set_text(c.row_label[row], vigie_criterion_label(cr.id));
            set_text(c.row_value[row], value);
            set_hidden(c.row_label[row], false);
            set_hidden(c.row_value[row], false);
            ++row;
        }
    }
    for (; row < kSeaRows; ++row) {
        set_hidden(c.row_label[row], true);
        set_hidden(c.row_value[row], true);
    }
}

void paint_legend(const char* status) {
    Screen& s = g_screen;
    const VigieVerdict verdicts[3] = {VigieVerdict::GOOD, VigieVerdict::POSSIBLE, VigieVerdict::POOR};
    for (int k = 0; k < 3; ++k) {
        set_bg_colour(s.legend_swatch[k], verdict_colour(verdicts[k]));
        set_text(s.legend_label[k], vigie_verdict_label(verdicts[k]));
    }
    set_bg_colour(s.legend_swatch[3], kLine);
    set_text(s.legend_label[3], vigie_tr(VigieText::SEA_NIGHT));
    // The age of the data takes the place of the legend's explanation when a warning holds the banner.
    set_text(s.legend_hint, status != nullptr ? status : vigie_tr(VigieText::SEA_LEGEND_BARS));
    set_text_colour(s.legend_hint, status != nullptr ? kYellow : kTextDim);
}

void paint_tide(const VigieSea& sea, int32_t offset, uint32_t utc, bool dim) {
    Screen& s = g_screen;
    char buf[96];
    set_opa(s.tide_panel, dim ? LV_OPA_60 : LV_OPA_COVER);
    set_text(s.tide_title, vigie_tr(VigieText::SEA_TIDE));

    // Coefficient of the next high water: an estimate of Vigie, said so.
    const int coef = utc != 0 ? vigie_sea_coefficient_for(sea, utc) : -1;
    const bool badge = coef >= 0 && vigie_format_tide_coefficient(sea.coefficients[coef], buf, sizeof(buf));
    set_hidden(s.tide_badge, !badge);
    if (badge) set_text(s.tide_badge_text, buf);

    // The curve over the 24 hours of the strips, a half cosine between the extremes the frame gives.
    // Only where the frame says: nothing before its first extreme nor after its last.
    const uint32_t t0 = sea.first_hour_utc;
    const bool have = t0 != 0 && sea.tide_count >= 2;
    float heights[kTideSamples] = {};
    int first = -1;
    int last = -1;
    float lo = 0.0f;
    float hi = 0.0f;
    if (have) {
        for (int k = 0; k < kTideSamples; ++k) {
            float h = 0.0f;
            if (!vigie_tide_height_at(sea, t0 + static_cast<uint32_t>(k) * 1800u, h)) {
                if (first >= 0) break;  // the first stretch the frame covers; no bridge over a gap
                continue;
            }
            heights[k] = h;
            if (first < 0) {
                first = k;
                lo = hi = h;
            }
            last = k;
            if (h < lo) lo = h;
            if (h > hi) hi = h;
        }
    }
    const bool curve = first >= 0 && last > first;
    set_hidden(s.tide_line, !curve);
    set_hidden(s.tide_empty, sea.tide_count != 0);
    set_text(s.tide_empty, vigie_tr(VigieText::SEA_NO_TIDE));
    if (curve) {
        const float span = hi - lo < 0.1f ? 0.1f : hi - lo;
        const int margin = sc(6);
        lv_point_precise_t points[kTideSamples];
        int n = 0;
        for (int k = first; k <= last; ++k) {
            // Samples are contiguous between the first and the last extreme: no hole to bridge.
            points[n].x = k * kTideCurveW / (kTideSamples - 1);
            points[n].y = margin + static_cast<int>((hi - heights[k]) / span * static_cast<float>(kTideCurveH - 2 * margin));
            ++n;
        }
        if (n != s.tide_point_count || std::memcmp(points, s.tide_points, static_cast<size_t>(n) * sizeof(points[0])) != 0) {
            std::memcpy(s.tide_points, points, static_cast<size_t>(n) * sizeof(points[0]));
            s.tide_point_count = n;
            lv_line_set_points(s.tide_line, s.tide_points, static_cast<uint32_t>(n));
        }
        // Where the curve is now.
        float now_height = 0.0f;
        const bool marker = utc >= t0 && utc <= t0 + 86400u && vigie_tide_height_at(sea, utc, now_height);
        set_hidden(s.tide_now_line, !marker);
        set_hidden(s.tide_now_dot, !marker);
        if (marker) {
            const int x = kTideCurveX + static_cast<int>(static_cast<uint64_t>(utc - t0) * static_cast<uint64_t>(kTideCurveW) / 86400u);
            const int y = kTideCurveY + margin + static_cast<int>((hi - now_height) / span * static_cast<float>(kTideCurveH - 2 * margin));
            set_pos(s.tide_now_line, x - 1, kTideCurveY);
            set_area(s.tide_now_dot, x - sc(6), y - sc(6), sc(12));
        }
    } else {
        set_hidden(s.tide_now_line, true);
        set_hidden(s.tide_now_dot, true);
    }

    // The next two high or low waters, in the clock of the zone.
    const int next = utc != 0 ? vigie_sea_next_tide(sea, utc) : -1;
    for (int j = 0; j < 2; ++j) {
        const bool known = next >= 0 && next + j < sea.tide_count && vigie_format_tide_extreme(sea.tide[next + j], offset, buf, sizeof(buf));
        set_hidden(s.tide_next[j], !known);
        if (known) set_text(s.tide_next[j], buf);
    }
}

void paint_sun(const VigieSea& sea, int32_t offset, bool dim) {
    Screen& s = g_screen;
    char buf[16];
    set_opa(s.sun_panel, dim ? LV_OPA_60 : LV_OPA_COVER);
    set_text(s.sun_title, vigie_tr(VigieText::SEA_SUN));
    const VigieText names[2] = {VigieText::SEA_SUNRISE, VigieText::SEA_SUNSET};
    const uint32_t times[2] = {sea.sunrise_utc, sea.sunset_utc};
    for (int k = 0; k < 2; ++k) {
        const bool known = times[k] != 0 && vigie_format_clock(times[k], offset, buf, sizeof(buf));
        set_hidden(s.sun_label[k], !known);
        set_hidden(s.sun_value[k], !known);
        if (!known) continue;
        set_text(s.sun_label[k], vigie_tr(names[k]));
        set_text(s.sun_value[k], buf);
    }
    set_text(s.sea_notice, vigie_tr(VigieText::SEA_RULES_NOTICE));  // every verdict is a rule of Vigie
}

void close_detail() {
    g_screen.detail_activity = -1;
    set_hidden(g_screen.detail, true);
}

void paint_detail(int i, const VigieSea& sea, int32_t offset) {
    Screen& s = g_screen;
    const VigieActivityForecast& a = sea.activities[i];
    char buf[160];
    const uint32_t colour = verdict_colour(a.verdict);
    set_text(s.detail_icon, activity_icon(i));
    set_text_colour(s.detail_icon, colour);
    set_text(s.detail_title, vigie_format_detail_title(static_cast<VigieActivity>(i), buf, sizeof(buf)) ? buf : "");
    char hour[40];
    if (vigie_format_detail_hour(a.detail_utc, offset, hour, sizeof(hour))) {
        std::snprintf(buf, sizeof(buf), "%s \xC2\xB7 %s", sea.name, hour);
    } else {
        std::snprintf(buf, sizeof(buf), "%s", sea.name);
    }
    set_text(s.detail_subtitle, buf);
    set_text(s.detail_verdict, vigie_verdict_label(a.verdict));
    set_text_colour(s.detail_verdict, colour);
    set_text(s.detail_score, vigie_format_score(a.points, a.out_of, buf, sizeof(buf)) ? buf : "");
    const VigieText heads[4] = {VigieText::SEA_COL_CRITERION, VigieText::SEA_COL_VALUE, VigieText::SEA_COL_SCORE,
                                VigieText::SEA_COL_READING};
    for (int k = 0; k < 4; ++k) set_text(s.detail_heads[k], vigie_tr(heads[k]));

    for (int r = 0; r < kVigieMaxCriteria; ++r) {
        CriterionRow& row = s.criteria[r];
        const bool used = r < a.criterion_count;
        set_hidden(row.label, !used);
        set_hidden(row.value, !used);
        set_hidden(row.score, !used);
        set_hidden(row.reading, !used);
        for (lv_obj_t* block : row.blocks) set_hidden(block, true);
        set_hidden(row.dot, true);
        if (!used) continue;
        const VigieCriterion& cr = a.criteria[r];
        const uint32_t cc = verdict_colour(cr.verdict);
        char value[32];
        char points[16];
        set_text(row.label, vigie_criterion_label(cr.id));
        set_text(row.value, vigie_format_criterion_value(cr, value, sizeof(value)) ? value : kDash);
        // Points when the criterion gives some (surf): blocks and "2/2". Else its verdict as a dot.
        // A criterion that does not count stays a dash: unknown is never shown as 0.
        const bool has_points = vigie_format_criterion_points(cr, points, sizeof(points));
        for (int b = 0; b < kDetailBlocks; ++b) {
            const bool block = has_points && b < cr.max_points;
            set_hidden(row.blocks[b], !block);
            if (block) set_bg_colour(row.blocks[b], b < cr.points ? cc : kLine);
        }
        const bool dot = !has_points && cr.verdict != VigieVerdict::UNKNOWN;
        set_hidden(row.dot, !dot);
        if (dot) set_bg_colour(row.dot, cc);
        set_text(row.score, has_points ? points : (dot ? "" : kDash));
        set_text_colour(row.score, has_points ? cc : kTextDim);
        set_text(row.reading, vigie_criterion_state_text(cr.id, cr.state));
    }
    set_text(s.detail_notice, vigie_tr(VigieText::SEA_RULES_NOTICE));
}

void paint_sea(const VigieWorldState& state, uint32_t utc) {
    Screen& s = g_screen;
    const VigieSea& sea = g_vigie_sea;
    const int32_t offset = zone_offset(state);
    const VigieSeaFreshness fresh = vigie_sea_freshness(sea, utc);
    char text[128];
    const VigieSeaBanner kind = vigie_format_sea_banner(sea, fresh, utc, offset, text, sizeof(text));
    paint_banner(kind, sea, text);

    // Too old or nothing: no verdict is shown. Some minutes old: shown, dimmed, with its age.
    const bool show = fresh == VigieSeaFreshness::FRESH || fresh == VigieSeaFreshness::STALE;
    const bool dim = fresh == VigieSeaFreshness::STALE;
    for (int i = 0; i < static_cast<int>(VigieActivity::COUNT_); ++i) paint_card(i, sea, show, dim, offset, utc);

    char status[96];
    const bool warning_holds_banner = kind == VigieSeaBanner::WARNING || kind == VigieSeaBanner::WARNING_UNKNOWN;
    const bool status_in_legend = warning_holds_banner && vigie_format_sea_status(sea, fresh, utc, status, sizeof(status));
    paint_legend(status_in_legend ? status : nullptr);
    paint_tide(sea, offset, utc, dim);
    paint_sun(sea, offset, dim);

    // The detail of an activity that is no longer judged (data gone old) closes by itself.
    if (s.detail_activity >= 0) {
        if (judged(sea.activities[s.detail_activity], show) && sea.activities[s.detail_activity].criterion_count > 0) {
            paint_detail(s.detail_activity, sea, offset);
        } else {
            close_detail();
        }
    }
}

// ─── Pages and touch ────────────────────────────────────────────────────────

// The header of the page on screen: its title, where the status sits, the switch to the other page.
void paint_page_header() {
    Screen& s = g_screen;
    const VigieSea& sea = g_vigie_sea;
    const bool on_sea = s.page == Page::SEA;
    if (on_sea) {
        set_text(s.brand, sea.name[0] != '\0' ? sea.name : vigie_tr(VigieText::SEA_PAGE));
        set_width(s.brand, sc(210));
        if (lv_obj_get_style_text_letter_space(s.brand, LV_PART_MAIN) != 0) lv_obj_set_style_text_letter_space(s.brand, 0, 0);
        set_pos(s.status_dot, sc(268), sc(22));
        set_pos(s.status_text, sc(286), sc(17));
        set_width(s.status_text, sc(176));  // ends before the page switch
    } else {
        set_text(s.brand, "VIGIE");
        set_width(s.brand, sc(76));
        if (lv_obj_get_style_text_letter_space(s.brand, LV_PART_MAIN) != sc(2)) lv_obj_set_style_text_letter_space(s.brand, sc(2), 0);
        set_pos(s.status_dot, sc(128), sc(22));
        set_pos(s.status_text, sc(146), sc(17));
        set_width(s.status_text, sc(310));
    }
    // The switch exists once there is a sea to show; the chevron points where the other page lies.
    const bool offer = sea.valid;
    set_hidden(s.page_switch, !offer);
    if (!offer) return;
    set_text(s.page_switch_text, vigie_tr(on_sea ? VigieText::WORLD_PAGE : VigieText::SEA_PAGE));
    set_text(s.page_switch_icon, on_sea ? "\U000F0141" : "\U000F0142");  // chevron-left / chevron-right
    // On the sea page the chevron points left, to the world; on the world page right, to the sea.
    set_pos(s.page_switch_icon, on_sea ? sc(6) : sc(78), sc(4));
    set_pos(s.page_switch_text, on_sea ? sc(34) : sc(14), sc(5));
}

// Repaint with the last known time, after a touch.
void repaint_after_touch() {
    const uint32_t now = esphome::millis();
    const uint32_t utc = g_screen.last_utc == 0 ? 0 : g_screen.last_utc + (now - g_screen.last_ms) / 1000;
    vigie_screen_paint(g_vigie_state, g_vigie_link, now, utc);
}

// The finger moved: what ends it opens nothing (a swipe is not a tap).
bool finger_swiped() {
    lv_indev_t* indev = lv_indev_active();
    return indev != nullptr && (lv_indev_get_press_moved(indev) || lv_indev_get_gesture_dir(indev) != LV_DIR_NONE);
}

void go_to_page(Page page) {
    Screen& s = g_screen;
    if (page == Page::SEA && !g_vigie_sea.valid) return;  // no sea page without a sea
    if (s.page == page) return;
    s.page = page;
    close_detail();
    repaint_after_touch();
}

void on_side_clicked(lv_event_t*) {
    if (finger_swiped()) return;
    vigie_next_zone(g_vigie_state);
    repaint_after_touch();
}

void on_page_switch_clicked(lv_event_t*) {
    if (finger_swiped()) return;
    go_to_page(g_screen.page == Page::SEA ? Page::WORLD : Page::SEA);
}

// A swipe to the left goes to the sea page, a swipe to the right back to the world.
void on_gesture(lv_event_t*) {
    if (g_screen.detail_activity >= 0) return;  // the popup is open: it has its own way out
    lv_indev_t* indev = lv_indev_active();
    if (indev == nullptr) return;
    const lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_LEFT) go_to_page(Page::SEA);
    if (dir == LV_DIR_RIGHT) go_to_page(Page::WORLD);
}

void open_detail(int activity) {
    Screen& s = g_screen;
    if (activity < 0 || activity >= static_cast<int>(VigieActivity::COUNT_)) return;
    const VigieActivityForecast& a = g_vigie_sea.activities[activity];
    if (!a.present || !a.available || a.verdict == VigieVerdict::UNKNOWN || a.criterion_count == 0) return;
    s.detail_activity = activity;
    set_hidden(s.detail, false);
    repaint_after_touch();
}

void on_card_clicked(lv_event_t* e) {
    if (finger_swiped()) return;
    const int* index = static_cast<const int*>(lv_event_get_user_data(e));
    if (index != nullptr) open_detail(*index);
}

void on_detail_clicked(lv_event_t*) { close_detail(); }

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
    // Fixed width, so that a long name of a spot is cut with "…" and never wraps; paint_page_header() sets it.
    s.brand = label(scr, f.body_bold, kText, sc(48), sc(15), sc(76));
    lv_obj_set_style_text_letter_space(s.brand, sc(2), 0);
    lv_label_set_text(s.brand, "VIGIE");
    s.status_dot = box(scr, sc(128), sc(22), sc(10), sc(10), kTextDim, LV_RADIUS_CIRCLE);
    s.status_text = label(scr, f.small, kTextDim, sc(146), sc(17), sc(310));
    const int clock_x = kScreenW - kMargin - sc(100);                // 7B: 912
    const int date_x = clock_x - sc(12) - sc(340);                   // 7B: 560
    s.date = label(scr, f.body, kTextDim, date_x, sc(15), sc(340), LV_TEXT_ALIGN_RIGHT);
    s.clock = label(scr, f.title, kText, clock_x, sc(11), sc(100), LV_TEXT_ALIGN_RIGHT);

    // The switch to the other page: a pill between the status and the date, at the same place on both pages.
    // Hidden until the frame has carried a sea (paint_page_header()).
    s.page_switch = box(scr, sc(470), sc(10), sc(110), sc(34), kPanel, LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(s.page_switch, 1, 0);
    lv_obj_set_style_border_color(s.page_switch, lv_color_hex(kLine), 0);
    lv_obj_add_flag(s.page_switch, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s.page_switch, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s.page_switch, on_page_switch_clicked, LV_EVENT_CLICKED, nullptr);
    s.page_switch_text = label(s.page_switch, f.body, kAccent, sc(14), sc(5));
    s.page_switch_icon = label(s.page_switch, f.icons, kAccent, sc(78), sc(4));
    // A swipe anywhere (the children let it bubble up to the screen) changes the page.
    lv_obj_add_event_cb(scr, on_gesture, LV_EVENT_GESTURE, nullptr);

    // The world page: everything the screen showed before the sea page, in one container so that one flag hides it.
    s.world_page = box(scr, 0, 0, kScreenW, kScreenH, kBackground, 0, false);

    // Map.
    s.map = box(s.world_page, kMapX, kMapY, kVigieMapWidth, kVigieMapHeight, kOcean, sc(10));
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
    lv_obj_t* alerts = box(s.world_page, kMapX, kAlertsY, kAlertsW, kAlertsH, kPanel, sc(10));
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
    s.side = box(s.world_page, kSideX, kMapY, kSideW, kSideH, kPanel, sc(10));
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

    // ─── Sea page ───────────────────────────────────────────────────────────
    s.sea_page = box(scr, 0, 0, kScreenW, kScreenH, kBackground, 0, false);
    lv_obj_add_flag(s.sea_page, LV_OBJ_FLAG_HIDDEN);

    // Banner: the coastal warning, or what is wrong with the data. Hidden when there is nothing to say.
    s.banner = box(s.sea_page, kMargin, kSeaBannerY, kScreenW - 2 * kMargin, kSeaBannerH, kPanel, sc(10));
    lv_obj_set_style_border_width(s.banner, sc(2), 0);
    lv_obj_set_style_border_color(s.banner, lv_color_hex(kLine), 0);
    lv_obj_add_flag(s.banner, LV_OBJ_FLAG_HIDDEN);
    s.banner_icon = label(s.banner, f.icons, kTextDim, sc(12), sc(5));
    lv_label_set_text(s.banner_icon, "\U000F0026");  // alert
    s.banner_text = label(s.banner, f.body, kText, sc(46), sc(7), kScreenW - 2 * kMargin - sc(60));

    // Four cards: surf, kite, boat, shore fishing.
    for (int i = 0; i < static_cast<int>(VigieActivity::COUNT_); ++i) {
        SeaCard& c = s.cards[i];
        c.index = i;
        const int x = kMargin + i * (kSeaCardW + kSeaGap);
        c.box = box(s.sea_page, x, kSeaCardsY, kSeaCardW, kSeaCardH, kPanel, sc(10));
        lv_obj_add_event_cb(c.box, on_card_clicked, LV_EVENT_CLICKED, &c.index);
        const int inner = kSeaCardInner;
        c.icon = label(c.box, f.icons, kTextDim, kSeaCardPad, sc(14));
        lv_label_set_text(c.icon, activity_icon(i));
        c.name = label(c.box, f.body_bold, kText, kSeaCardPad + sc(34), sc(14), inner - sc(34));
        c.verdict = label(c.box, f.title, kTextDim, kSeaCardPad, sc(44), inner);
        c.reason = label(c.box, f.small, kTextDim, kSeaCardPad, sc(80), inner);
        c.slot_label = label(c.box, f.small, kTextDim, kSeaCardPad, sc(110), inner);
        c.slot = label(c.box, f.body_bold, kText, kSeaCardPad, sc(130), inner);
        for (int h = 0; h < kVigieSeaHours; ++h) {
            c.bars[h] = box(c.box, kSeaStripX + h * kSeaBarPitch, kSeaStripY, kSeaBarPitch - 1, kSeaStripH, kLine, 1);
            lv_obj_add_flag(c.bars[h], LV_OBJ_FLAG_HIDDEN);
        }
        c.now = box(c.box, kSeaStripX, kSeaStripY - sc(4), 2, kSeaStripH + sc(8), kWhite, 0);
        lv_obj_add_flag(c.now, LV_OBJ_FLAG_HIDDEN);
        // The clock at the start of the strip, 12 hours later and at its end.
        const int axis_w = sc(56);
        const int axis_y = kSeaStripY + kSeaStripH + sc(5);
        c.axis[0] = label(c.box, f.small, kTextDim, kSeaStripX, axis_y, axis_w);
        c.axis[1] = label(c.box, f.small, kTextDim, kSeaStripX + kSeaStripW / 2 - axis_w / 2, axis_y, axis_w, LV_TEXT_ALIGN_CENTER);
        c.axis[2] = label(c.box, f.small, kTextDim, kSeaStripX + kSeaStripW - axis_w, axis_y, axis_w, LV_TEXT_ALIGN_RIGHT);
        for (int r = 0; r < kSeaRows; ++r) {
            const int y = kSeaRowsY + r * kSeaRowH;
            c.row_label[r] = label(c.box, f.small, kTextDim, kSeaCardPad, y + sc(2), kSeaLabelW);
            c.row_value[r] = label(c.box, f.body_bold, kText, kSeaCardPad + inner - kSeaValueW, y, kSeaValueW, LV_TEXT_ALIGN_RIGHT);
        }
    }

    // Legend: what the colours and the bars say.
    const int legend_pitch = sc(116);
    for (int k = 0; k < 4; ++k) {
        const int x = kMargin + k * legend_pitch;
        s.legend_swatch[k] = box(s.sea_page, x, kSeaLegendY + sc(5), sc(10), sc(10), kLine, sc(2));
        s.legend_label[k] = label(s.sea_page, f.small, kTextDim, x + sc(16), kSeaLegendY, legend_pitch - sc(20));
    }
    const int hint_x = kMargin + 4 * legend_pitch + sc(8);
    s.legend_hint = label(s.sea_page, f.small, kTextDim, hint_x, kSeaLegendY, kScreenW - kMargin - hint_x, LV_TEXT_ALIGN_RIGHT);

    // Tide: title, the next two extremes, the estimated coefficient, the curve of the 24 hours.
    s.tide_panel = box(s.sea_page, kMargin, kSeaBottomY, kSeaTideW, kSeaBottomH, kPanel, sc(10));
    s.tide_title = label(s.tide_panel, f.body_bold, kText, kSeaCardPad, sc(10), sc(84));
    // The next two extremes, in a row under the curve.
    const int next_w = (kSeaTideW - 2 * kSeaCardPad) / 2;
    for (int j = 0; j < 2; ++j) {
        s.tide_next[j] = label(s.tide_panel, f.small, kTextDim, kSeaCardPad + j * next_w, kSeaBottomH - sc(30), next_w);
    }
    const int badge_w = sc(210);  // "Coefficient 83 (estimated)" is the longest of the two languages
    s.tide_badge = box(s.tide_panel, kSeaTideW - kSeaCardPad - badge_w, sc(8), badge_w, sc(26), kAccent, LV_RADIUS_CIRCLE);
    s.tide_badge_text = label(s.tide_badge, f.small, kBackground, 0, sc(2), badge_w, LV_TEXT_ALIGN_CENTER);
    s.tide_empty = label(s.tide_panel, f.body, kTextDim, kSeaCardPad, kTideCurveY + kTideCurveH / 2 - sc(10),
                         kSeaTideW - 2 * kSeaCardPad, LV_TEXT_ALIGN_CENTER);
    s.tide_line = lv_line_create(s.tide_panel);
    lv_obj_remove_style_all(s.tide_line);
    lv_obj_set_pos(s.tide_line, kTideCurveX, kTideCurveY);
    lv_obj_set_size(s.tide_line, kTideCurveW, kTideCurveH);
    lv_obj_set_style_line_width(s.tide_line, sc(3), 0);
    lv_obj_set_style_line_color(s.tide_line, lv_color_hex(kAccent), 0);
    lv_obj_set_style_line_rounded(s.tide_line, true, 0);
    lv_obj_remove_flag(s.tide_line, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s.tide_line, LV_OBJ_FLAG_HIDDEN);
    s.tide_now_line = box(s.tide_panel, kTideCurveX, kTideCurveY, 2, kTideCurveH, kYellow, 0);
    s.tide_now_dot = box(s.tide_panel, kTideCurveX, kTideCurveY, sc(12), sc(12), kYellow, LV_RADIUS_CIRCLE);
    lv_obj_add_flag(s.tide_now_line, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s.tide_now_dot, LV_OBJ_FLAG_HIDDEN);

    // Sun, and the line that says every verdict is a rule of Vigie.
    s.sun_panel = box(s.sea_page, kMargin + kSeaTideW + kSeaGap, kSeaBottomY, kSeaSunW, kSeaBottomH, kPanel, sc(10));
    s.sun_title = label(s.sun_panel, f.body_bold, kText, kSeaCardPad, sc(10), kSeaSunW - 2 * kSeaCardPad);
    for (int k = 0; k < 2; ++k) {
        const int y = sc(42) + k * sc(36);
        s.sun_label[k] = label(s.sun_panel, f.body, kTextDim, kSeaCardPad, y + sc(3), sc(100));
        s.sun_value[k] = label(s.sun_panel, f.title, kText, kSeaCardPad + sc(110), y, sc(110));
    }
    s.sea_notice = label(s.sun_panel, f.small, kTextDim, kSeaCardPad, kSeaBottomH - sc(30), kSeaSunW - 2 * kSeaCardPad);

    // ─── Detail popup: the criteria behind a verdict (touch anywhere closes it) ──
    s.detail = box(scr, 0, 0, kScreenW, kScreenH, 0x000000, 0);
    lv_obj_set_style_bg_opa(s.detail, LV_OPA_70, 0);
    lv_obj_add_flag(s.detail, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s.detail, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s.detail, on_detail_clicked, LV_EVENT_CLICKED, nullptr);
    const int pop_w = kScreenW - 2 * sc(40);
    const int pop_h = sc(432);
    const int pop_pad = sc(20);
    lv_obj_t* pop = box(s.detail, sc(40), (kScreenH - pop_h) / 2, pop_w, pop_h, kPanel, sc(14));
    lv_obj_set_style_border_width(pop, 1, 0);
    lv_obj_set_style_border_color(pop, lv_color_hex(kLine), 0);
    s.detail_icon = label(pop, f.icons, kTextDim, pop_pad, sc(16));
    s.detail_title = label(pop, f.title, kText, pop_pad + sc(36), sc(12), sc(230));
    s.detail_subtitle = label(pop, f.small, kTextDim, pop_pad + sc(280), sc(20), pop_w - pop_pad - sc(280) - sc(60) - pop_pad);
    lv_obj_t* close_icon = label(pop, f.icons, kTextDim, pop_w - pop_pad - sc(24), sc(16));
    lv_label_set_text(close_icon, "\U000F0156");  // close
    s.detail_verdict = label(pop, f.title, kText, pop_pad, sc(62), sc(230));
    s.detail_score = label(pop, f.body_bold, kText, pop_pad + sc(240), sc(66), sc(300));
    // Columns: criterion, value, points, reading.
    const int col_x[4] = {pop_pad, sc(250), sc(470), sc(670)};
    const int col_w[4] = {sc(220), sc(210), sc(190), pop_w - sc(670) - pop_pad};
    for (int k = 0; k < 4; ++k) s.detail_heads[k] = label(pop, f.small, kTextDim, col_x[k], sc(108), col_w[k]);
    box(pop, pop_pad, sc(132), pop_w - 2 * pop_pad, 1, kLine, 0);
    for (int r = 0; r < kVigieMaxCriteria; ++r) {
        CriterionRow& row = s.criteria[r];
        const int y = sc(140) + r * sc(40);
        row.label = label(pop, f.body_bold, kText, col_x[0], y, col_w[0]);
        row.value = label(pop, f.body, kText, col_x[1], y, col_w[1]);
        for (int b = 0; b < kDetailBlocks; ++b) {
            row.blocks[b] = box(pop, col_x[2] + b * sc(26), y + sc(8), sc(22), sc(10), kLine, sc(2));
        }
        row.dot = box(pop, col_x[2], y + sc(5), sc(14), sc(14), kLine, LV_RADIUS_CIRCLE);
        row.score = label(pop, f.body_bold, kText, col_x[2] + 4 * sc(26) + sc(6), y, sc(80));
        row.reading = label(pop, f.small, kTextDim, col_x[3], y + sc(3), col_w[3]);
    }
    s.detail_notice = label(pop, f.small, kTextDim, pop_pad, pop_h - pop_pad - sc(18), pop_w - 2 * pop_pad);
    s.built = true;
}

void vigie_screen_paint(const VigieWorldState& state, const VigieLinkStatus& link, uint32_t now_ms, uint32_t utc) {
    if (!g_screen.built) return;
    Screen& s = g_screen;
    s.last_ms = now_ms;
    s.last_utc = utc;
    // The sea page needs a sea: with none, the world page is what is shown.
    if (s.page == Page::SEA && !g_vigie_sea.valid) {
        s.page = Page::WORLD;
        close_detail();
    }
    set_hidden(s.world_page, s.page != Page::WORLD);
    set_hidden(s.sea_page, s.page != Page::SEA);
    paint_page_header();
    paint_status(state, link, now_ms, utc);
    if (s.page == Page::SEA) {
        paint_sea(state, utc);
        return;
    }
    paint_map(state);
    paint_alerts(state, utc);
    paint_weather(state, utc);
}

void vigie_screen_show_page(VigieScreenPage page) {
    if (!g_screen.built) return;
    go_to_page(page == VigieScreenPage::SEA ? Page::SEA : Page::WORLD);
}

void vigie_screen_open_detail(int activity) {
    if (!g_screen.built) return;
    open_detail(activity);
}
