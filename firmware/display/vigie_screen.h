/**
 * @file vigie_screen.h
 * @brief The Vigie screen (LVGL 9.5), built in C++ once and repainted from the world state.
 *
 * Layout for 1024×600 (Waveshare 7B): status bar on top; world map with the active
 * hazards (dot colour = severity) and the watched zones; the five most severe alerts
 * under the map; on the right, the weather of the active zone. Touching the weather panel
 * shows the next zone (distances are then measured from it).
 * Every text comes from firmware/core/vigie_text.* (English or French). Writes compare
 * first: an unchanged text or colour does not invalidate anything.
 */
#pragma once

#include <cstdint>

#include "vigie_api_client.h"
#include "vigie_model.h"

namespace esphome {
namespace font {
class Font;
}
namespace image {
class Image;
}
}  // namespace esphome

struct VigieScreenFonts {
    const esphome::font::Font* small = nullptr;         // 16 px, regular
    const esphome::font::Font* body = nullptr;          // 20 px, regular
    const esphome::font::Font* body_bold = nullptr;     // 20 px, bold
    const esphome::font::Font* title = nullptr;         // 26 px, bold
    const esphome::font::Font* temperature = nullptr;   // 64 px, bold, digits only
    const esphome::font::Font* icons = nullptr;         // MDI 24 px
    const esphome::font::Font* weather_icon = nullptr;  // MDI 72 px
};

// Builds every object on the active screen. Call once, after LVGL is set up.
void vigie_screen_build(const VigieScreenFonts& fonts, esphome::image::Image* world_map);

// Repaints what changed. `utc` = current time (0 if unknown), `now_ms` = millis().
void vigie_screen_paint(const VigieWorldState& state, const VigieLinkStatus& link, uint32_t now_ms, uint32_t utc);
