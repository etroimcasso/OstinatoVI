#include "render/battle/battle_message.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "data/battle_gfx_tables.h"
#include "render/battle/layout.h"
#include "render/battle/menu_window.h"
#include "render/color_gradient.h"
#include "render/layers.h"
#include "render/text/glyph.h"
#include "text/layout.h"

namespace ostinato::render {

namespace {

const BattleWindowEntry& box(battle::MessageWindow window, Language language) {
    return battleWindow(window == battle::MessageWindow::Wide ? BattleWindowId::WIDE_MESSAGE
                                                               : BattleWindowId::NARROW_MESSAGE,
                        language);
}

int textLeft(const battle::ShownMessage& message) {
    const int column =
        message.window == battle::MessageWindow::Wide ? kWideMessageTextColumn : kNarrowMessageTextColumn;
    return column * kBattleCellPx + message.centering;
}

int textColumns(battle::MessageWindow window) {
    return window == battle::MessageWindow::Wide ? kWideMessageTextColumns : kNarrowMessageTextColumns;
}

}  // namespace

Cells MessageWindowBox(battle::MessageWindow window, Language language, const retropp::AtlasManifest& wallpaper,
                       retropp::PaletteId colors) {
    const BattleWindowEntry& place = box(window, language);
    return MenuWindow(place.width, place.height, wallpaper, colors);
}

int messageWindowColumns(battle::MessageWindow window, Language language) { return box(window, language).width; }

int messageWindowRows(battle::MessageWindow window, Language language) { return box(window, language).height; }

retropp::LayerScroll messageWindowScroll(battle::MessageWindow window, Language language) {
    const BattleWindowEntry& place = box(window, language);
    return {.x = -place.column * kBattleCellPx, .y = -place.row * kBattleCellPx};
}

Regions MessageShading(battle::MessageWindow window, Language language) {
    const BattleWindowEntry& place = box(window, language);
    std::vector<GradientBand> bands;
    for (int band = 0; band < kMessageShadingBands; ++band) {
        bands.push_back({.rows = kMessageShadingBandRows, .level = static_cast<std::uint8_t>(band),
                         .blend = retropp::BlendMode::Subtract});
    }
    return ColorGradient(kMessageShadingKey,
                         {.x = static_cast<float>(place.column * kBattleCellPx),
                          .y = static_cast<float>(kMessageShadingTopPx)},
                         static_cast<float>(place.width * kBattleCellPx), bands);
}

Sprites MessageText(const battle::ShownMessage& message, const retropp::AtlasManifest& glyphs, retropp::PaletteId ink,
                    retropp::PaletteId shadow) {
    const int left = textLeft(message) - text::kGlyphArtLeftOfPen;
    const int top = kMessageTextTopPx + text::kGlyphRowOffset;
    Sprites sprites;
    for (std::size_t n = 0; n < message.glyphs.size(); ++n) {
        const battle::ShownGlyph& glyph = message.glyphs[n];
        const Sprites letter = Glyph(std::string{kMessageGlyphKey} + ":" + std::to_string(n), glyph.glyph,
                                     left + glyph.x, top - glyph.rise, glyphs, ink, shadow);
        sprites.insert(sprites.end(), letter.begin(), letter.end());
    }
    return sprites;
}

Regions MessageTextClip(const battle::ShownMessage& message) {
    Regions clip = retropp::stencil(
        retropp::ShapePoints::rectangle(
            {.x = static_cast<float>(textLeft(message)), .y = static_cast<float>(kMessageTextTopPx)},
            static_cast<float>(textColumns(message.window) * kBattleCellPx), static_cast<float>(kMessageLineHeightPx)),
        retropp::StencilMode::TransparentOutside);
    for (retropp::Region& region : clip) {
        region.key = kMessageTextClipKey;
    }
    return clip;
}

}  // namespace ostinato::render
