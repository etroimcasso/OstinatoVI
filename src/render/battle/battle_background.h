// BattleBackground — the battlefield's tiles.
//
//     TileLayer(kBattlefieldKey, kBattlefieldZ, BattleBackground(picture, sheet), kBackgroundColumns,
//               kBackgroundRowsShown, {.scroll = {.y = kBattlefieldScrollPx}});
//
// The background's tilemap as tile cells, its first 19 rows, every cell drawn from `sheet` — the
// background's decoded tiles uploaded as one sheet — through the one of its three palettes the cell
// names, flipped as the cell says. A cell naming a tile none of the background's blocks holds draws
// the sheet's blank tile. The battlefield shows the tilemap's pixel rows 1 to 151 on lines 0 to 150,
// so the layer scrolls down by kBattlefieldScrollPx and its last row ends on line 150.
#pragma once

#include <array>
#include <cstdint>

#include "retropp/atlas_manifest.h"
#include "retropp/palette.h"

#include "data/battle_background_art.h"
#include "render/types.h"

namespace ostinato::render {

// A background's decoded tiles as uploaded: the sheet, with one blank tile after the background's
// own (`blankTile`), and the three palettes the tilemap's cells name, in order.
struct BackgroundSheet {
    retropp::AtlasManifest tiles;
    std::uint16_t blankTile = 0;
    std::array<retropp::PaletteId, kBackgroundPalettes> palettes{};
};

// PRECONDITION (asserted): every shown cell names one of the three palettes, as every cartridge
// background's does.
[[nodiscard]] Cells BattleBackground(const BattleBackgroundPicture& picture, const BackgroundSheet& sheet);

}  // namespace ostinato::render
