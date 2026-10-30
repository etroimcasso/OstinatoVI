#include "render/battle/hud_windows.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "data/battle_gfx_tables.h"
#include "render/battle/layout.h"
#include "render/battle/menu_window.h"
#include "render/color_gradient.h"
#include "render/layers.h"

namespace ostinato::render {

namespace {

const BattleWindowEntry& leftWindow(Language language) {
    return battleWindow(BattleWindowId::MONSTER_NAMES, language);
}
const BattleWindowEntry& rightWindow(Language language) {
    return battleWindow(BattleWindowId::PARTY_STATUS, language);
}

}  // namespace

int hudWindowsColumns(Language language) {
    const BattleWindowEntry& right = rightWindow(language);
    return right.column + right.width - leftWindow(language).column;
}

retropp::LayerScroll hudWindowsScroll(Language language) {
    return {.x = -leftWindow(language).column * kBattleCellPx, .y = -kHudTopPx};
}

Cells HudWindows(Language language, const retropp::AtlasManifest& wallpaper, retropp::PaletteId colors) {
    const BattleWindowEntry& left = leftWindow(language);
    const BattleWindowEntry& right = rightWindow(language);
    const int columns = hudWindowsColumns(language);
    const Cells leftCells = MenuWindow(left.width, left.height, wallpaper, colors);
    const Cells rightCells = MenuWindow(right.width, right.height, wallpaper, colors);
    Cells cells(static_cast<std::size_t>(columns * kHudWindowRows));
    const auto place = [&](const Cells& window, const BattleWindowEntry& box) {
        for (int row = 0; row < box.height; ++row) {
            for (int column = 0; column < box.width; ++column) {
                cells[static_cast<std::size_t>(row * columns + box.column - left.column + column)] =
                    window[static_cast<std::size_t>(row * box.width + column)];
            }
        }
    };
    place(leftCells, left);
    place(rightCells, right);
    return cells;
}

Regions HudShading(Language language) {
    // The bands run from line 151 to the windows' last line.
    int rowsLeft = kHudTopPx + kHudWindowRows * kBattleCellPx - kHudShadingTopPx;
    std::vector<GradientBand> bands;
    for (int band = 0; band < kHudShadingBands && rowsLeft > 0; ++band) {
        const int rows = std::min(kHudShadingBandRows, rowsLeft);
        bands.push_back({.rows = rows, .level = static_cast<std::uint8_t>(band), .blend = retropp::BlendMode::Subtract});
        rowsLeft -= rows;
    }
    return ColorGradient(kHudShadingKey,
                         {.x = static_cast<float>(leftWindow(language).column * kBattleCellPx),
                          .y = static_cast<float>(kHudShadingTopPx)},
                         static_cast<float>(hudWindowsColumns(language) * kBattleCellPx), bands);
}

}  // namespace ostinato::render
