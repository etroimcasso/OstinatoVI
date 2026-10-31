#include "render/battle/hud_text.h"

#include <cstddef>
#include <string>

#include "text/small_font.h"

namespace ostinato::render {

Sprites HudText(std::string_view key, std::span<const std::uint8_t> codes, int x, int y,
                retropp::PaletteId palette, const retropp::AtlasManifest& smallFont) {
    Sprites sprites;
    for (std::size_t i = 0; i < codes.size(); ++i) {
        if (codes[i] == kHudBlank) {
            continue;
        }
        sprites.push_back(retropp::Sprite{
            .key = std::string{key} + ":" + std::to_string(i),
            .x = x + static_cast<int>(i) * text::kSmallGlyphPx,
            .y = y,
            .size = {text::kSmallGlyphPx, text::kSmallGlyphPx},
            .atlas = smallFont.atlasId,
            .tile = smallFont[codes[i]].tile,
            .palette = palette,
        });
    }
    return sprites;
}

}  // namespace ostinato::render
