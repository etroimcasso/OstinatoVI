#include "render/battle/hud_text.h"

#include <cstddef>
#include <string>

#include "render/layers.h"
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

Regions HudTextClip() {
    Regions regions;
    for (int line = 0; line < kHudTextLines; ++line) {
        const float hiddenRow = static_cast<float>(hudLineY(line) + kHudGlyphRowsShown);
        regions.push_back(retropp::Region{
            .key = std::string{kHudTextClipKey} + ":" + std::to_string(line),
            .shape = retropp::ShapePoints::rectangle({.x = 0.0f, .y = hiddenRow}, static_cast<float>(kScreenSize.width),
                                                     static_cast<float>(text::kSmallGlyphPx - kHudGlyphRowsShown)),
            .effects = {retropp::ScreenSpaceEffect{.kind = retropp::ScreenSpaceEffectKind::Transparency}},
        });
    }
    return regions;
}

}  // namespace ostinato::render
