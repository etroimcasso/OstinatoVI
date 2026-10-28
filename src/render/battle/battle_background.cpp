#include "render/battle/battle_background.h"

#include <cassert>
#include <cstddef>

#include "render/battle/layout.h"
#include "render/layers.h"

namespace ostinato::render {

Cells BattleBackground(const BattleBackgroundPicture& picture, const BackgroundSheet& sheet) {
    Cells cells;
    cells.reserve(static_cast<std::size_t>(kBackgroundColumns * kBackgroundRowsShown));
    for (std::size_t cell = 0; cell < static_cast<std::size_t>(kBackgroundColumns * kBackgroundRowsShown);
         ++cell) {
        const BackgroundCell& c = picture.cells[cell];
        assert(c.palette() >= kBackgroundFirstPalette &&
               c.palette() < kBackgroundFirstPalette + kBackgroundPalettes &&
               "a shown cell names one of the background's three palettes");
        const std::optional<std::size_t> tile = picture.sheetTile(c.tile());
        const std::size_t slot = tile ? sheet.tiles[*tile].tile : sheet.blankTile;
        cells.push_back(retropp::TileCell{
            .atlas = sheet.tiles.atlasId,
            .tile = static_cast<std::uint16_t>(slot),
            .palette = sheet.palettes[c.palette() - kBackgroundFirstPalette],
            .flipX = c.flippedX(),
            .flipY = c.flippedY(),
        });
    }
    return cells;
}

Regions BattlefieldClip() {
    return {retropp::Region{
        .key = kBattlefieldClipKey,
        .shape = retropp::ShapePoints::rectangle(
            {.x = 0.0f, .y = static_cast<float>(kBattlefieldHeightPx)},
            static_cast<float>(kScreenSize.width),
            static_cast<float>(kBackgroundRowsShown * kBattleCellPx - kBattlefieldHeightPx)),
        .effects = {retropp::ScreenSpaceEffect{.kind = retropp::ScreenSpaceEffectKind::Transparency}},
    }};
}

}  // namespace ostinato::render
